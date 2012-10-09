/************************************************************************
 *
 *			                >>>>>>> SYNGRANUL~ <<<<<<<
 *
 *						   	forme d'onde synth�tique
 *                        multi-buffer enveloppe externe,
 *              continuite des grains lors d'un changement de buffer.
 *                         controle en float et/ou en audio
 *                           ----------------------
 *                              GMEM 2002-2004
 *         Laurent Pottier / Loic Kessous / Charles Bascou / Leopold Frey
 *
 * -----------------------------------------------------------------------
 *
 * 
 * N.B. 
 *      * Pour compatibilite maximum Mac/PC :
 *        pas d'accents dans les commentaires svp
 *
 *      * Faites des commentaires !
 *
 ************************************************************************/

#define ARCH_PD

#ifdef ARCH_PD
	#include "m_pd.h"
#else
	#include "ext.h"
	#include "z_dsp.h"
	#include "buffer.h"
#endif

#ifndef TWOPI
#define TWOPI		6.28318530717958647692
#endif  // TWOPI

#ifdef ARCH_PD
	#define t_plong float
	#define A_SYM A_SYMBOL
	#define A_LONG A_FLOAT
	#define SETSYM SETSYMBOL
	#define SETLONG SETFLOAT
	#define w_sym w_symbol
	#define w_long w_float
	#define sysmem_newptr getbytes
	
#else
	#define t_plong long
#endif

#include <math.h>
#define NVOICES 512 // nombre maximum de voix
#define NEBUF 64	// nombre maximum de buffers enveloppe
#define TABLESIZE 512

#define MIN_LENGTH 0.1 // DISABLED

#define MAGIC_CIRCLE 1

#define MIN(a,min) (a < min) ? a : min
#define MAX(a,max) (a > max) ? a : max
#define MOD(x,m) ( ( x = fmod(x,m) ) < 0. ) ? x+m : x 
#define MAX(a,max) (a > max) ? a : max

// INTERPOLATION TRICK DEFINES ///    TODO : EXPLAIN

#define TABLE_BITS 10
#define TABLE_SIZE (1 << TABLE_BITS)

#define LOST_BITS 	6

#define FRAC_BITS (TABLE_BITS + LOST_BITS)
#define FRAC_SIZE (1 << FRAC_BITS)

// INTERPOLATION COEFF STRUCTURE

typedef struct linear_interp
{
	float a;
	float b;
	
} t_linear_interp; 

// INTERPOLATION TRICK MACRO

#define interp_index_scale(f) 	((f) * FRAC_SIZE)
#define interp_get_int(i) 	 	((i) >> FRAC_BITS)
#define interp_get_frac(i)		((i) & (FRAC_SIZE - 1))

#define interp_get_table_index(i)	(((i) >> LOST_BITS) & (TABLE_SIZE - 1))


// Le mode perf_debug cree une 3-5-7-9eme sortie a l'objet
// Cette sortie trace ses etats utiles (si [poll 1])
//
//			* voices
//			* enveloppe active
//          * buffer son actif
//			* longueur du buffer son actif
//          * noms des buffers son et enveloppe actif (pour ne pas s'y perdre)
//			* nom du fichier dans le buffer actif (idem)
//
#define PERF_DEBUG 1

// STRUCTURE DE L'OBJET
void *synGranul_class;

typedef struct _synGranul
{
	#ifdef ARCH_PD
    	t_object x_obj;
	#else
		t_pxobject x_obj;
	#endif
	
// PARAMETRE DE WAVEFORM
	float 	 x_sintable[TABLESIZE];


// BUFFER ENVELOPPE
#ifdef ARCH_PD
    t_garray *x_env_buf[NEBUF];			// le buffer~ enveloppe
#else
	t_buffer *x_env_buf[NEBUF];
#endif

    t_symbol *x_env_sym[NEBUF];			// le symbol correspondant au nom du buffer~ enveloppe
    long	  x_env_frames[NEBUF];		// le nombre de sample dans le buffer~ enveloppe
	float	 *x_env_samples[NEBUF];		// pointeur sur le tableau d'�chantillon du buffer~ enveloppe
	int		  x_nenvbuffer;				// nombre de buffer enveloppe
 	int       x_active_env;				// le buffer enveloppe actif est celui dans lequel les nouveaux grains sont pr�lev�s.

// PARAMETRES D'HIVERS
    int		  x_nvoices;				// nombre de voix
    int		  x_sinterp;				// type d'interpolation
    long      x_nouts;					// nombre de sorties (2-4-6-8)
    int		  x_nvoices_active;			// nombre de voix actives
     
// GRAINS NON-SIGNAL
    int		  x_askfor;					// etat signalant une demande de grain (par un bang)
										// Parametre en mode non-signal
	float     x_freq, x_phase, x_amp, x_pan, x_length, x_dist, x_teta;	
	 									// Spatialisation : gain par hp
    float     x_hp1, x_hp2, x_hp3, x_hp4, x_hp5, x_hp6, x_hp7, x_hp8;
    
// GRAINS SIGNAL
    long  *x_ind;				// indice de chaque voix
    long  *x_remain_ind;		// nombre d'ech restant a calculer pour chaque grain	  
										// vecteurs des voix (pitch, amp, pan, len...)
    float *Vfreq, *Vphase, *Vamp,
		  *Vlength, *Vpan, *Vdist; 
	float *Vphase_inc;				// pas d'increment de la phase du sinus	
	
	float *Vmem1,*Vmem2;// memoires internes des oscilateurs ( fast sine calculation MAGIC CIRCLE )
	float *Vcoef;
	
    int   x_env_dir;					// direction de lecture de l'enveloppe
    float *envinc;				// pas d'avancement dans le buffer enveloppe (par rapport � la longueur du grain
	float *envind;				// indice de d�part dans le buffer enveloppe (debut si lecture normale, fin si lecture inversee)
	float *x_env;				// enveloppe de chaque voix 
    int   *Venv;				// numero du buffer enveloppe dans lequel sera pris le grain

    int   *x_delay;				// delay du declenchement de grain par rapport au vecteur (voir perform)
    int   *x_voiceOn;			// voix active
    
										// valeur de pan pour chaque voix
    float *Vhp1, *Vhp2, *Vhp3, *Vhp4, *Vhp5, *Vhp6, *Vhp7, *Vhp8;

// INTERPOLATION COEF TABLE

	t_linear_interp * x_linear_interp_table;
     
// DSP
    float x_sr;						// frequence d'echantillonnage
									// booleens vrais si signal connecte sur l'entree i
    short x_in2con, x_in3con, x_in4con, x_in5con, x_in6con, x_in7con, x_in8con; 
    float x_sigin;					// valeur a l'entree de type signal	(voir dsp)
#ifdef ARCH_PD
    float x_f; /// dummy pd
#endif

} t_synGranul;

//%%%%%%%%%%%%%%%%%% declaration des routines %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%//

// Instanciation
void *synGranul_new(t_symbol *s, short ac, t_atom *av);	// routine de creation d'une instance de l'objet

int synGranul_alloc(t_synGranul *x);
int synGranul_desalloc(t_synGranul *x);

// Buffer son
void synGranul_set(t_synGranul *x,t_symbol *msg, short ac, t_atom * av);		// definition des buffer~ son 
//void synGranul_setInit(t_synGranul *x, t_symbol *s);	// definition du buffer~ son (� l'initialisation du patch)
void synGranul_swap(t_synGranul *x);					// bascule buffer son
int synGranul_bufferinfos(t_synGranul *x);				// affectation des param�tres des buffers
//void synGranul_nbuffer(t_synGranul *x, long n);		// nombre de buffer son

// Bufffer enveloppe
void synGranul_setenv(t_synGranul *x, t_symbol *msg, short ac, t_atom * av);	// definition le buffer~ enveloppe

#ifdef ARCH_PD
void synGranul_envbuffer(t_synGranul *x, float n);		// # du buffer env actif
void synGranul_nenvbuffer(t_synGranul *x, float n);		// nombre de buffer enveloppe
#else
void synGranul_envbuffer(t_synGranul *x, long n);		// # du buffer env actif
void synGranul_nenvbuffer(t_synGranul *x, long n);		// nombre de buffer enveloppe
#endif

// Init
void synGranul_init(t_synGranul *x);					// initialisation des buffers
// Grains non-signal
void synGranul_bang(t_synGranul *x);					// routine pour un bang, declenchement d'un grain
void synGranul_grain(t_synGranul *x, t_symbol *s, short ac, t_atom *av); // declenchement d'un grain par liste
void synGranul_float(t_synGranul *x, double f);			// routine de recuperation des parametres non-signal

// Parametres
#ifdef ARCH_PD
void synGranul_sinterp(t_synGranul *x, float n);		// interpollation dans la lecture du buffer
void synGranul_poll(t_synGranul *x, float n);		// nombres de voix en sorties
void synGranul_clear(t_synGranul *x);				// panique ! effacement des grains en cours
void synGranul_nvoices(t_synGranul *x, float n);			// definition du nombre de voix (polyphonie)
void synGranul_tellme(t_synGranul *x);					// demande d'information sur l'etat de l'objet
#else
void synGranul_sinterp(t_synGranul *x, long n);		// interpollation dans la lecture du buffer
void synGranul_poll(t_synGranul *x, long n);		// nombres de voix en sorties
void synGranul_clear(t_synGranul *x);				// panique ! effacement des grains en cours
void synGranul_nvoices(t_synGranul *x, long n);			// definition du nombre de voix (polyphonie)
void synGranul_tellme(t_synGranul *x);					// demande d'information sur l'etat de l'objet
#endif


// spatialisation
float synGranul_spat (float x, float d, int n);
float synGranul_spat2 (float x, float d);
void synGranul_panner(t_synGranul *x, float f);
void synGranul_pannerV(t_synGranul *x, int voice);
//void synGranul_dist(t_synGranul *x, double f);			// modification de la distance (pour le pan)

void synGranul_assist(t_synGranul *x, void *b, long m, long a, char *s);	// assistance info inlet, out1et

// DSP
#ifdef ARCH_PD
void synGranul_dsp(t_synGranul *x, t_signal **sp);			// signal processing
#else
void synGranul_dsp(t_synGranul *x, t_signal **sp, short *count);			// signal processing
#endif
void synGranul_free(t_synGranul *x);										// liberation de la memoire
t_int *synGranul_perform1(t_int *w);	// synthese du signal pour 1 sortie
t_int *synGranul_perform2(t_int *w);	// synthese du signal pour 2 sorties
t_int *synGranul_perform4(t_int *w);	// synthese du signal pour 4 sorties
t_int *synGranul_perform6(t_int *w);	// synthese du signal pour 6 sorties
t_int *synGranul_perform8(t_int *w);	// synthese du signal pour 8 sorties

// fonctions de lecture buffer
	//prototype (pointeur de fonction)
float (*synGranul_readbuff)(float *, double, int);//, float); 
	//hold
float synGranul_readhold(float * buf, double index, int skip);//, float coef_sr);
	//interpolation lineaire
float synGranul_readlin(float * buf, double index, int skip);//, float coef_sr);

t_symbol *ps_buffer;

// utilit
float atom2float(t_atom * av, int ind);
t_symbol* atom2symbol(t_atom * av, int ind);

// Debug
#ifdef PERF_DEBUG
    void  *info;
    t_atom  info_list[5];
    int ask_poll;
#ifdef ARCH_PD
	void synGranul_info(t_synGranul *x,float n);
#else
	void synGranul_info(t_synGranul *x,long n);
#endif
#endif

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%//

#ifdef ARCH_PD
// for pd
void synGranul_tilde_setup(void)
{ 
	synGranul_class = class_new(gensym("synGranul~"), (t_newmethod)synGranul_new, (t_method)synGranul_free, (short)sizeof(t_synGranul), 0L, A_GIMME, 0);

	CLASS_MAINSIGNALIN(synGranul_class, t_synGranul, x_f);
	         
    class_addmethod(synGranul_class, (t_method)synGranul_setenv, gensym("setenv"), A_GIMME, 0);	// definition du buffer~ enveloppe
	class_addmethod(synGranul_class, (t_method)synGranul_envbuffer, gensym("envbuffer"), A_FLOAT, 0);	// # env buffer actif     

	class_addmethod(synGranul_class, (t_method)synGranul_nvoices, gensym("nvoices"), A_FLOAT, 0);	// nombre de voix (polyphonie)       
 	class_addmethod(synGranul_class, (t_method)synGranul_tellme, gensym("tellme"),0);				// demande d'info
    class_addbang(synGranul_class, (t_method)synGranul_bang);    						// routine pour un bang, declenchement d'un grain
    #ifndef ARCH_PD
    addfloat((t_method)synGranul_float);							// affectation des valeurs par des flottants
   	#endif
   	
	//class_addmethod((t_method)synGranul_assist, gensym("assist"), A_CANT, 0);		// assistance in out 
    class_addmethod(synGranul_class, (t_method)synGranul_dsp, gensym("dsp"), 0);			// signal processing
    class_addmethod(synGranul_class, (t_method)synGranul_sinterp, gensym("sinterp"), A_FLOAT, 0);	// interpollation dans la lecture du buffer pour �viter les clics
    class_addmethod(synGranul_class, (t_method)synGranul_clear, gensym("clear"),0);				// panique ! effacement des grains en cours
    class_addmethod(synGranul_class, (t_method)synGranul_clear, gensym("panic"),0);				// panique ! effacement des grains en cours

	 class_addmethod(synGranul_class, (t_method)synGranul_grain, gensym("grain"), A_GIMME, 0);
	 
#ifdef PERF_DEBUG
    class_addmethod(synGranul_class, (t_method)synGranul_poll, gensym("poll"), A_FLOAT, 0);
    class_addmethod(synGranul_class, (t_method)synGranul_info, gensym("info"), A_FLOAT, 0);
#endif
   CLASS_MAINSIGNALIN(synGranul_class, t_synGranul, x_f);

	post("Copyright � 2012 synGranul~ v2.0 Zorglub GMEM Marseille F") ;
	post("build %s",__DATE__);
}


#else
// for max
void main(void)
{ 
	setup((t_messlist **)&synGranul_class, (method)synGranul_new, (method)synGranul_free, (short)sizeof(t_synGranul), 0L, A_GIMME, 0);
                 
    addmess((method)synGranul_setenv, "setenv", A_GIMME, 0);	// definition du buffer~ enveloppe
	addmess((method)synGranul_envbuffer, "envbuffer", A_LONG, 0);	// # env buffer actif     

    
	addmess((method)synGranul_nvoices, "nvoices", A_LONG, 0);	// nombre de voix (polyphonie)       
 	addmess((method)synGranul_tellme, "tellme",0);				// demande d'info
    addbang((method)synGranul_bang);    						// routine pour un bang, declenchement d'un grain
    addfloat((method)synGranul_float);							// affectation des valeurs par des flottants
   
	addmess((method)synGranul_assist, "assist", A_CANT, 0);		// assistance in out 
    addmess((method)synGranul_dsp, "dsp", A_CANT, 0);			// signal processing
    addmess((method)synGranul_sinterp, "sinterp", A_LONG, 0);	// interpollation dans la lecture du buffer pour �viter les clics
    addmess((method)synGranul_clear, "clear",0);				// panique ! effacement des grains en cours
    addmess((method)synGranul_clear, "panic",0);				// panique ! effacement des grains en cours
	addmess((method)synGranul_grain, "grain", A_GIMME, 0);

#ifdef PERF_DEBUG
    addmess((method)synGranul_poll, "poll", A_LONG, 0);
    addmess((method)synGranul_info, "info", A_LONG, 0);
#endif
    
    dsp_initclass();
	ps_buffer = gensym("buffer~");

	post("Copyright � 2005 synGranul~ v2.0 Zorglub GMEM Marseille F") ;
	post("build %s",__DATE__);
}
#endif

// Reception d'un bang, declenche un grain
void synGranul_bang (t_synGranul *x)
{
	x->x_askfor = 1;
}

// declenchement grain par liste
// delay(ms) begin detune amp length pan dist buffer envbuffer
void synGranul_grain(t_synGranul *x, t_symbol *s, short ac, t_atom *av)
{
	int j,p;
	int nvoices = x->x_nvoices;
	int xn = 0;
	float fn, iphase;
	
	if(ac < 8)
	{	
		post("syngranul~ : grain args are <delay(ms)> <freq> <phase> <amp> <length> <pan> <dist> <envbuffer>");
		return;
	}

	
	double delay = atom2float(av,0);
	double freq = atom2float(av,1);
	double phase = atom2float(av,2);
	double amp = atom2float(av,3);
	double length = atom2float(av,4);
	double pan = atom2float(av,5);
	double dist = atom2float(av,6);
	
	int envbuffer = (int)atom2float(av,7);
	//int envbuffer = (int)atom2float(av,8);
	
	double srms = x->x_sr*0.001; // double for precision
	
//	begin = begin * srms;
	delay = delay * srms;
	p = nvoices;
	
	while(--p && x->x_voiceOn[p] ) { }  // avance jusqu a la 1ere voix libre
				
				if(p)
				{
    				x->x_voiceOn[p] = 1;													// activation
					
					x->Vfreq[p] = freq;						// frequence 
					x->Vphase[p] = MOD(phase,1.);		// phase
					x->Vphase_inc[p] = freq / x->x_sr;									// increment de phase
					
						// MAGIC CIRCLE
					fn = x->Vfreq[p]* TWOPI / x->x_sr;
					iphase = x->Vphase[p] * TWOPI;
					x->Vcoef[p] = 2. * cos(fn);
					x->Vmem1[p] = sin(iphase-fn);
					x->Vmem2[p] = sin(iphase-2*fn);
					
					x->Vamp[p]		= amp;						// amplitude
					x->Venv[p] =  bufferenv_check(x, envbuffer );	// enveloppe active pour ce grain
     
				
						if(length<0)
						{	
							x->Vlength[p]	= -length * srms;
							x->envinc[p]	= -1.*(float)(x->x_env_frames[x->Venv[p]] - 1.) / x->Vlength[p] ;
							x->envind[p]	= x->x_env_frames[x->Venv[p]] - 1;
						}
						else
						{
							x->Vlength[p]	= length * srms;
							x->envinc[p]	= (float)(x->x_env_frames[x->Venv[p]] - 1.) / x->Vlength[p] ;
							x->envind[p]	= 0. ;
						}
				
					
					x->Vpan[p]		=   pan;						// pan
					x->Vdist[p]		= dist;					// distance
					synGranul_pannerV(x,p);
					
					x->x_ind[p] = 0;
					x->x_remain_ind[p] = (long) x->Vlength[p];
					x->x_delay[p] = delay;   // delay de declenchement dans le vecteur         	  
    					
				}

	
}


// effacement de tous les grains en cours (panique)
void synGranul_clear (t_synGranul *x)
{
	int i;
	for (i=0; i< NVOICES; i++) x->x_voiceOn[i] = 0;
}

// voir en sortie le nombre de voix, le buffer son actif etc...
#ifdef PERF_DEBUG
	void synGranul_poll(t_synGranul *x, t_plong n)
	{
		ask_poll = (n > 0 ? 1 : 0);
	}
	
#endif
/*
// Modification du parametre de distance pour le pan
void synGranul_dist(t_synGranul *x, double f)
{
	// Distance pour pan
	f = (f > 1. ? 1. : f) ;
	f = (f < 0. ? 0. : f) ;
	x->x_dist = f ;
  
//	post("dist : %lf",f);
	synGranul_panner(x,x->x_teta);
}//*/

float atom2float(t_atom * av, int ind)
{
	switch (av[ind].a_type)
		{
			case A_FLOAT :
				return av[ind].a_w.w_float;
				break;
#ifndef ARCH_PD
			case A_LONG :
				return av[ind].a_w.w_long;
				break;
#endif
			default :
				return 0.;
				
		}


}

t_symbol * atom2symbol(t_atom * av, int ind)
{
	switch (av[ind].a_type)
		{
			case A_SYM :
				return av[ind].a_w.w_sym;
				break;
			default :
				return 0;
				
		}


}


// Fonction de verification de la validit� du buffer demand�
int bufferenv_check(t_synGranul *x, int num)
{
	return ( x->x_env_buf[num] == 0 ) ? 0 : num ;
	
}


// Fonction pour spatialiser sur n voix
float synGranul_spat (float x, float d, int n)
{
	float x1;
	
	x1 = fmod(x, 1.0);	
	x1 = (x1 < 0.5 ? 0.5 - x1 : x1 - 0.5) ;		// x1 = abs(x - 0.5) ;
	x1 = (1 - (x1 * n * d)) ;					// fct lineaire ;
	x1 = (x1 < 0 ? 0 : x1) ;					//  max(0, x1 );
	return pow(x1 , 0.5) * ((d / 2) + 0.5) ;	// (racine de x1) compensee pour la distance;
}

// Fonction pour spatialiser sur 2 voix
float synGranul_spat2 (float x, float d)
{
	float x1;
	
	x1 = fmod(x, 1.0) ;
	x1 = (x1 < 0.5 ? 0.5 - x1 : x1 - 0.5) ;		// x1 = abs(x - 0.5) ;
	x1 = (1 - (x1 * 2 * d)) ; 					// fct lineaire ;
	x1 = (x1 < 0 ? 0 : x1) ; 					//  max(0, (1 - (x1 * 2)) ;
//	return pow(x1 , 0.5) * ((d / 2) + 0.5) ;	// (racine de x1) compensee pour la distance;
	return pow(x1 * ((d / 2) + 0.5) , 0.5)  ;	// (racine de x1) compensee pour la distance
}

// Panning suivant nombres de hps (2-4-6-8)
void synGranul_panner(t_synGranul *x, float teta)
{  
	int n = x->x_nouts;
	float delta = 1./(float)n ;
	float d = x->x_dist;		
	teta = (teta < 0 ? 0 : teta) ;
	x->x_teta = teta;			
	switch (n)
	{
		case 2:
			x->x_hp1 = synGranul_spat2( teta + 0.5, d) ;	
			x->x_hp2 = synGranul_spat2( teta + 0.0, d) ;
		break;
		
		case 4:
			x->x_hp1 = synGranul_spat( teta + 0.5, d, n) ;	
			x->x_hp2 = synGranul_spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->x_hp3 = synGranul_spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->x_hp4 = synGranul_spat( teta + 0.5 + (1 * delta), d, n) ;
		break;
		
		case 6:
			x->x_hp1 = synGranul_spat( teta + 0.5, d, n) ;	
			x->x_hp2 = synGranul_spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->x_hp3 = synGranul_spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->x_hp4 = synGranul_spat( teta + 0.5 - (3 * delta), d, n) ;	
			x->x_hp5 = synGranul_spat( teta + 0.5 + (2 * delta), d, n) ;	
			x->x_hp6 = synGranul_spat( teta + 0.5 + (1 * delta), d, n) ;	
		break;
		
		case 8 :	
			x->x_hp1 = synGranul_spat( teta + 0.5, d, n) ;	
			x->x_hp2 = synGranul_spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->x_hp3 = synGranul_spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->x_hp4 = synGranul_spat( teta + 0.5 - (3 * delta), d, n) ;	
			x->x_hp5 = synGranul_spat( teta + 0.5 - (4 * delta), d, n) ;	
			x->x_hp6 = synGranul_spat( teta + 0.5 + (3 * delta), d, n) ;	
			x->x_hp7 = synGranul_spat( teta + 0.5 + (2 * delta), d, n) ;	
			x->x_hp8 = synGranul_spat( teta + 0.5 + (1 * delta), d, n) ;	
		break;
	}
}

// Panning suivant nombres de hps par voix(2-4-6-8)
void synGranul_pannerV(t_synGranul *x, int voice)
{  
	int n = x->x_nouts;
	float delta = 1./(float)n ;
	float d = ((x->Vdist[voice] < 0 ? 0 : x->Vdist[voice]) > 1 ? 1 : x->Vdist[voice]);
	float teta = (x->Vpan[voice] < 0 ? 0 : x->Vpan[voice]) ;
	switch (n)
	{
		case 2:
			x->Vhp1[voice] = synGranul_spat2( teta + 0.5, d) ;	
			x->Vhp2[voice] = synGranul_spat2( teta + 0.0, d) ;
		break;
		
		case 4:
			x->Vhp1[voice] = synGranul_spat( teta + 0.5, d, n) ;	
			x->Vhp2[voice] = synGranul_spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->Vhp3[voice] = synGranul_spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->Vhp4[voice] = synGranul_spat( teta + 0.5 + (1 * delta), d, n) ;
		break;
		
		case 6:
			x->Vhp1[voice] = synGranul_spat( teta + 0.5, d, n) ;	
			x->Vhp2[voice] = synGranul_spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->Vhp3[voice] = synGranul_spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->Vhp4[voice] = synGranul_spat( teta + 0.5 - (3 * delta), d, n) ;	
			x->Vhp5[voice] = synGranul_spat( teta + 0.5 + (2 * delta), d, n) ;	
			x->Vhp6[voice] = synGranul_spat( teta + 0.5 + (1 * delta), d, n) ;	
		break;
		
		case 8 :	
			x->Vhp1[voice] = synGranul_spat( teta + 0.5, d, n) ;	
			x->Vhp2[voice] = synGranul_spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->Vhp3[voice] = synGranul_spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->Vhp4[voice] = synGranul_spat( teta + 0.5 - (3 * delta), d, n) ;	
			x->Vhp5[voice] = synGranul_spat( teta + 0.5 - (4 * delta), d, n) ;	
			x->Vhp6[voice] = synGranul_spat( teta + 0.5 + (3 * delta), d, n) ;	
			x->Vhp7[voice] = synGranul_spat( teta + 0.5 + (2 * delta), d, n) ;	
			x->Vhp8[voice] = synGranul_spat( teta + 0.5 + (1 * delta), d, n) ;	
		break;
	}
}

#ifndef ARCH_PD
// Reception des valeurs sur les entrees non-signal
void synGranul_float(t_synGranul *x, double f)
{
	if (x->x_obj.z_in == 0)
		post("a float in first inlet don't do anything");
	else if (x->x_obj.z_in == 1)
		x->x_freq = f ;
	else if (x->x_obj.z_in == 2)
		x->x_phase = MOD(f,1);
	else if (x->x_obj.z_in == 3)
		x->x_amp = (f>0.) ? f : 0. ;
	else if (x->x_obj.z_in == 4){	 if(f<0)
									 {
										x->x_length = (-f > MIN_LENGTH)? -f : MIN_LENGTH;
										x->x_env_dir = -1;
									 } else {
										x->x_length= (f > MIN_LENGTH)? f : MIN_LENGTH;
										x->x_env_dir = 1;
									 }
								}
	else if (x->x_obj.z_in == 5){	 
									x->x_pan = f;
									synGranul_panner(x, x->x_pan);
									// necessaire si on ne fait pas le calcul
									// dans la perform quand signal non connecte
								}		
	else if (x->x_obj.z_in == 6){	 
									x->x_dist = f;
									synGranul_panner(x, x->x_pan);
									// necessaire si on ne fait pas le calcul
									// dans la perform quand signal non connecte
								}
	else if (x->x_obj.z_in == 7){	 
									//x->x_active_buf = buffer_check(x,(int)f);
									// necessaire si on ne fait pas le calcul
									// dans la perform quand signal non connecte
								}		
}
#endif

// limitation de la polyphonie
void synGranul_nvoices(t_synGranul *x, t_plong n) 
{ 
	n = (n > 0) ? n : 1 ;
	x->x_nvoices = (n < NVOICES) ? n+1 : NVOICES; 
}

// interpollation dans la lecture du buffer (evite clic, crack et autre pop plop)
void synGranul_sinterp(t_synGranul *x, t_plong n)
{ 
	x->x_sinterp = n ? 1 : 0 ; 
}

// Impression de l'etat de l'objet
void synGranul_tellme(t_synGranul *x)
{
	int i;
	post("  ");
	post("_______synGranul~'s State_______"); 

	post("::global settings::");

	post("outputs : %ld",x->x_nouts);
    post("voices : %ld",x->x_nvoices);
	post("________________________________");


	post("::envelope buffers::");

    post("-- Active envelope buffer : %ld --",x->x_active_env+1);
	post("-- Number of envelope buffers : %ld --",x->x_nenvbuffer);
	for (i=0; i< x->x_nenvbuffer ; i++)
	{
		if(x->x_env_sym[i]->s_name) post("   envelope buffer~ %ld name : %s",i+1,x->x_env_sym[i]->s_name);
	}
	post("________________________________");
	
	synGranul_info(x,-1);

}

// Informations en sortie de l'objet
#ifdef PERF_DEBUG
	void synGranul_info (t_synGranul *x, t_plong n)
	{
		// Si n=	0 infos sur buffer son
		//			1 infos sur buffer enveloppe
		//		   -1 infos sur buffers...
		int i;
		
		if(ask_poll)
		{
			
			if(n==1 || n==-1)
			{
				SETSYM( info_list, gensym("active_env"));
				SETFLOAT(info_list+1,  x->x_active_env+1 ); 
				outlet_list(info,0l,2,info_list);
				
				for(i=0; i<x->x_nenvbuffer; i++)
				{
					if(x->x_env_sym[i])
					{
						SETSYM(info_list, gensym("envbuffer"));
						SETLONG(info_list+1, i);
						SETSYM(info_list+2, x->x_env_sym[i]); 
						outlet_list(info,0l,3,info_list);
					}
				}
			}
		}
}
	

#endif

//$$$$$$$$$$$$ routine de lecture buffer $$$$$$$$$$$$$$$$$$$$$$$$$$$$$//

float synGranul_readhold(float * buf, double index, int skip)
{   
	return buf[((long)index)*skip];
}

float synGranul_readlin(float * buf, double index, int skip)
{   
	float	finter;		// interpolation index
    int		baseind;
    float	pval, nval;

    baseind = (int)index;
  	finter = index - baseind ;
  	baseind *= skip;
  	pval = buf[baseind];
	nval = buf[baseind+skip];
	return  pval + (nval - pval) * finter ;
}

//NNNNNNN routine de creation de l'objet NNNNNNNNNNNNNNNNNNNNNNNNNNNN//

void *synGranul_new(t_symbol *s, short ac, t_atom *av)
{   
	int i,j,symcount = 0, k; 
	float f;
#ifdef ARCH_PD
    	t_synGranul *x = (t_synGranul *)pd_new(synGranul_class);
#else
		t_synGranul *x = (t_synGranul *)newobject(synGranul_class);
#endif

    //creation des entres supplementaires---//
#ifdef ARCH_PD
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	x->x_f = 0;
#else
    dsp_setup((t_pxobject *)x,7);
#endif    
    x->x_nouts = 2;	// pour que nb outs = 2 si pas specifie
    
	if(ac < 1)
	{
		post("Missing arg #1 Envelop Buffer Name #2 Outputs number (1-2-4-6-8)");
		return 0;
	} else {

		//////////////arguments/////////////////
		// note : 1er symbol buffer son , 2eme symbol buffer env, 3eme nombres de sorties

		for (i=0; i < NEBUF; i++)
		{
			x->x_env_buf[i]=0;
    		x->x_env_sym[i]=gensym("empty_buffer");
			x->x_env_samples[i]=0;
			x->x_env_frames[i]=-1;
		}

		for (j=0; j < ac; j++){
    		switch (av[j].a_type){
    		
    			case A_LONG:
    				x->x_nouts = av[j].a_w.w_long;
    			break;
    			
    			case A_SYM:

    					x->x_env_sym[0] = av[j].a_w.w_sym;
				break;
#ifndef ARCH_PD
    			case A_FLOAT:
    			post("argument %ld is a float : %lf, not assigned", (long)j,av[j].a_w.w_float);
    			break;
#endif
    		}	
		}

#ifdef ARCH_PD
		//creation des sortie signal
		outlet_new(&x->x_obj, &s_signal);
		
		if (x->x_nouts > 1)
			outlet_new(&x->x_obj, &s_signal);
		  
		if (x->x_nouts > 2)
		{
			//x->x_nouts = 4;
			outlet_new(&x->x_obj, &s_signal);
			outlet_new(&x->x_obj, &s_signal);
		}
    	
		if (x->x_nouts > 4)
		{
			//x->x_nouts = 6;
			outlet_new(&x->x_obj, &s_signal);
			outlet_new(&x->x_obj, &s_signal);
		}
    	
		if (x->x_nouts > 6)
		{
			//x->x_nouts = 8;   	
			outlet_new(&x->x_obj, &s_signal);
			outlet_new(&x->x_obj, &s_signal);
		}
		
		#ifdef PERF_DEBUG
		info = outlet_new(&x->x_obj, &s_list);
		#endif
		
#else
		///////////////////////////////
		 // info outlet
		#ifdef PERF_DEBUG
		info = outlet_new((t_pxobject *)x, 0L);
		#endif
		
		//creation des sortie signal
		outlet_new((t_pxobject *)x, "signal");
		if (x->x_nouts > 1)
		{
			outlet_new((t_pxobject *)x, "signal");
		}  
		if (x->x_nouts > 2)
		{
			//x->x_nouts = 4;
			outlet_new((t_pxobject *)x, "signal");
			outlet_new((t_pxobject *)x, "signal");
		}
    	
		if (x->x_nouts > 4)
		{
			//x->x_nouts = 6;
			outlet_new((t_pxobject *)x, "signal");
			outlet_new((t_pxobject *)x, "signal");
		}
    	
		if (x->x_nouts > 6)
		{
			//x->x_nouts = 8;   	
			outlet_new((t_pxobject *)x, "signal");
			outlet_new((t_pxobject *)x, "signal");
		}
#endif		
		//allocations des tableaux
		if( !synGranul_alloc(x))
		{
			post("error synGranul~ : not enough memory to instanciate");
		}
		
		
		// initialisation des autres parametres
		x->x_nvoices = 64;
		x->x_askfor = 0;    
		x->x_freq = 440.;
		x->x_phase = 0.0;
		x->x_amp = 1.0;    
		x->x_length = 100;
    
		x->x_sinterp = 1;
    
		x->x_pan = 1/(x->x_nouts*2);
		x->x_dist = 1;
		synGranul_panner(x, x->x_pan); 
		x->x_nvoices_active = 0;  
		x->x_env_dir = 1;
        
		for (i=0; i< NVOICES; i++){       
     		x->x_ind[i] = -1;
     		x->x_env[i] = 0;
     		x->x_voiceOn[i] = 0;
		}

		// initialisation des parametres d'activite (modifie par la suite dans setenv et set)

		x->x_nenvbuffer = 1;
		x->x_active_env = 0;
		
		for( i=1 ; i<NEBUF ; i++)
		{
			x->x_env_buf[i] = 0;
			x->x_env_sym[i] = 0;
		}
		
		for( i=0 ; i<TABLESIZE ; i++)
		{
			x->x_sintable[i] = sin(TWOPI * (float)i/TABLESIZE);
		}
		
		// generation de la table de coeff d'interpolation
		x->x_linear_interp_table = (t_linear_interp *) sysmem_newptr( TABLE_SIZE * sizeof(struct linear_interp) );
		
		if( ! x->x_linear_interp_table)
		{
			post("synGranul~ : not enough memory to instanciate");
			return(0);
		}
		for(i=0; i<TABLE_SIZE ; i++)
		{
			f = (float)(i)/TABLE_SIZE; // va de 0 a 1
			
			x->x_linear_interp_table[i].a = 1 - f;
			x->x_linear_interp_table[i].b = f;
		}


		return (x);
	}

}
//NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN//

#ifndef ARCH_PD 
//--------assitance information inlet et out1et----------------------//
void synGranul_assist(t_synGranul *x, void *b, long m, long a, char *s)
{
	switch(m) {
		case 1: // inlet
			switch(a) {			
				case 0:
				sprintf(s, " bang,set, signal zero X-ing, et cetera)");		break;
				case 1:
				sprintf(s, " freq (float & signal) ");						break;				
				case 2:
				sprintf(s, " phase (float & signal) ");					break;
				case 3:
				sprintf(s, " amp (float & signal) ");						break;
				case 4:
				sprintf(s, " length (float & signal) ");					break;
				case 5:
				sprintf(s, " pan (float & signal) ");						break;																				
				case 6:
				sprintf(s, " dist (float & signal) ");						break;																		
			}
		break;
		case 2: // outlet
			switch(a) {
				case 0:
				sprintf(s, "(signal) Output 1");				break;				
				case 1:
					if(x->x_nouts > 1) sprintf(s, "(signal) Output 2");
					else sprintf(s, "info out (voices...)");
					break;
				case 2:
					if(x->x_nouts > 2) sprintf(s, "(signal) Output 3");
					else sprintf(s, "info out (voices...)");
					break;
				case 3:
				sprintf(s, "(signal) Output 4");				break;
				case 4:
					if(x->x_nouts > 4) sprintf(s, "(signal) Output 5");
					else sprintf(s, "info out (voices...)");
					break;		
				case 5:
				sprintf(s, "(signal) Output 6");				break;
				case 6:
					if(x->x_nouts > 6) sprintf(s, "(signal) Output 7");
					else sprintf(s, "info out (voices...)");
					break;		
				case 7:
				sprintf(s, "(signal) Output 8");				break;									
				case 8:
				sprintf(s, "info out (voices...)");				break;									
			}
		break;
	}
}

#endif

t_int *synGranul_perform1(t_int *w)
{   
	t_synGranul *x = (t_synGranul *)(w[1]);

    t_float * in			= (float *)(w[2]);
	t_float * t_freq 		= (float *)(w[3]);
    t_float * t_phase	 	= (float *)(w[4]);
    t_float * t_amp			= (float *)(w[5]);
    t_float * t_length 		= (float *)(w[6]);
    t_float * t_pan 		= (float *)(w[7]);
    t_float * t_dist 		= (float *)(w[8]);

    t_float *out1 = (t_float *)(w[9]);
      
    int n = (int)(w[10]);

	int t_buf;

    int N = n;
    int xn = 0, p;

	double srms = x->x_sr*0.001; // double for precision
	double indT;
	float val;
	float val1;
    float sigin, prevsigin,savein; // values of actual and previous input sample   
	
	float fn,iphase;
	
	float lengthind = x->x_length*srms;	
	
	// temporary buffer index variable
	int t_active_env;
	float * t_envsamples;

    int i,j;
	int nvoices = x->x_nvoices;
	
	int initTest;
	
	//variables grains
	
	float mem1,mem2;
	float coef;
	float amp;
	float xcross_offs;
	
	//variables boucles dsp
	
	int dsp_i_begin;
	int dsp_i_end;
	
	int nech_process;
	
	// interp variables
	
	long base_lindex_env;
	long lindex_env;
	long lincr_env;
	
	int buffer_index;
	int buffer_index_plus_1;
	int interp_table_index;
	
	t_linear_interp * interp_table = x->x_linear_interp_table;

	// debug var
#ifdef PERF_DEBUG
    int n_voices_active;
    
#endif

	sigin = x->x_sigin;
   	
#ifdef PERF_DEBUG
	if(ask_poll)
	{
		n_voices_active=0;
	
		for(i=0; i< nvoices; i++) if (x->x_voiceOn[i] ) n_voices_active++;
		
		SETSYM( info_list, gensym("voices"));
		SETLONG(info_list+1, n_voices_active); 
		outlet_list(info,0l,2,info_list);
	}
#endif

	initTest = synGranul_bufferinfos(x);
   	if(initTest == 0) goto zero;
	else
	{
		t_active_env = bufferenv_check(x,x->x_active_env);
		// %%%%%%%%%%     creation de grains   %%%%%%%%%%%%%%%%%
   			
   		p=nvoices;
   		
		// Dans le cas d'un grain d�clench� par un bang
		savein = in[0];
   		if(x->x_askfor)
   		{
			sigin = -1.;
			in[0] = 1.;
			x->x_askfor = 0;
   		}
		
		// Meme chose avec declenchement par zero-crossing sur tout le vecteur in, dans la limite fixee par nvoices
		while (n-- && p)
		{
			//-----signal zero-crossing ascendant declenche un grain----//
			prevsigin = sigin;
			sigin = in[xn];

			if (prevsigin <= 0 && sigin > 0) // creation d'un grain
			{
				//evite l'aliasing // *******DISABLED **** TO CONTINUE *****
				xcross_offs = prevsigin / (prevsigin - sigin );
				while(--p && x->x_voiceOn[p] ) { }  // avance jusqu a la 1ere voix libre
				
				if(p)
				{
						// affecte les valeurs au grain
					x->x_voiceOn[p] = 1;														// activation
					x->Vfreq[p] = (x->x_in2con) * t_freq[xn] + (1 - x->x_in2con) * x->x_freq;						// frequence 
					x->Vphase_inc[p] = x->Vfreq[p] / x->x_sr;									// increment de phase
					x->Vphase[p] = (x->x_in3con) ? MOD(t_phase[xn],1.) : x->x_phase; // phase
					x->Vphase[p] = x->Vphase[p] - xcross_offs * x->Vphase_inc[p];
					x->Vphase[p] = MOD(x->Vphase[p], 1.);
					
						// MAGIC CIRCLE
					fn = x->Vfreq[p]* TWOPI / x->x_sr;
					iphase = x->Vphase[p] * TWOPI;
					x->Vcoef[p] = 2. * cos(fn);
					x->Vmem1[p] = sin(iphase-fn);
					x->Vmem2[p] = sin(iphase-2*fn);
					
					
					x->Vamp[p]		= (x->x_in4con) * t_amp[xn] + (1 - x->x_in4con) * x->x_amp;						// amplitude
					x->Venv[p] = t_active_env;												// enveloppe active pour ce grain
					
					if(x->x_in5con)
					{
						if(t_length[xn]<0)
						{	
							x->Vlength[p]	= -t_length[xn]*srms;
							x->envinc[p]	= -1.*(float)(x->x_env_frames[t_active_env] - 1.) / x->Vlength[p] ;
							x->envind[p]	= x->x_env_frames[t_active_env] - 1 - x->envinc[p]* xcross_offs;
						}
						else
						{
							x->Vlength[p]	= t_length[xn]*srms;
							x->envinc[p]	= (float)(x->x_env_frames[t_active_env] - 1.) / x->Vlength[p] ;
							x->envind[p]	= 0. - x->envinc[p]* xcross_offs ;
						}
					}
					else
					{
						x->Vlength[p] = lengthind;
						x->envinc[p] = x->x_env_dir * x->x_env_frames[t_active_env] / x->Vlength[p] ;
						if(x->x_env_dir < 0)
							x->envind[p] =  x->x_env_frames[t_active_env] - 1 - x->envinc[p]* xcross_offs;
						else 
							x->envind[p] = 0. - x->envinc[p]* xcross_offs;
					}

					x->x_ind[p] = 0;
					x->x_remain_ind[p] = (long) x->Vlength[p];
					x->x_delay[p] = xn;   // delay de declenchement dans le vecteur         	  
    					
				} else goto perform;
			}
			xn++ ; // boucle on incremente le pointeur dans le vecteur in et le delay de declenchement
		}
   		 
		// %%%%%%%%%%     fin creation     %%%%%%%%%%%%%%%%%
   		
   		perform :
		in[0] = savein;
		
   		//&&&&&&&&&&&&&  Boucle DSP  &&&&&&&&&&&&&
   		n = N;
   		j=0;
		while (n--)
		{
			out1[j] = 0;
			j++;   
		}	
    		 	         		   				 									 
		for (i=0; i < nvoices; i++)
		{         
			//si la voix est en cours              
			if (x->x_voiceOn[i]) //&& x->x_ind[i] < x->Vlength[i] )
			{
				// si delay + grand que taille vecteur on passe � voix suivante 
				if(x->x_delay[i] >= N)
				{	
					x->x_delay[i] -= N ;
					goto next;
				}
				
				// nb d'ech a caluler pour ce vecteur
				nech_process = MIN( (N - x->x_delay[i]) , x->x_remain_ind[i] );
				
				
				dsp_i_begin = x->x_delay[i];
				dsp_i_end = x->x_delay[i] + nech_process;
				
				t_envsamples = x->x_env_samples[x->Venv[i]];
				
				// Enveloppe long index & incr calcul
				base_lindex_env = (long) x->envind[i];
				lindex_env = interp_index_scale( x->envind[i] - (double) base_lindex_env );
				lincr_env  = interp_index_scale(x->envinc[i]);
				
				x->envind[i] += nech_process * x->envinc[i];
				
				// Init des memoires algo sinus
				coef = x->Vcoef[i];
				mem1 = x->Vmem1[i];
				mem2 = x->Vmem2[i];
				
				amp = x->Vamp[i];
				
				
				//***********************************
				// CALCUL EFFECTIF DES ECHANTILLONS
				//***********************************
				
				//post("\nmax %i : ",x->x_env_frames[t_active_env]);
				
				
				if( x->x_sinterp == 1 ) // interp 
					for(j= dsp_i_begin; j < dsp_i_end; j++)
					{
					
						// Lecture de l'enveloppe
						buffer_index = base_lindex_env + interp_get_int( lindex_env );
						interp_table_index = interp_get_table_index( lindex_env );
		      			x->x_env[i] = interp_table[interp_table_index].a * t_envsamples[buffer_index]
		      							+ interp_table[interp_table_index].b * t_envsamples[buffer_index + 1];
		      			
						//post("%i:%d ",buffer_index,x->x_env[i]);
						
		      			lindex_env += lincr_env;
		      			
						// Calcul de la forme d'onde
						val = coef * mem1 - mem2;
						mem2 = mem1;
						mem1 = val;			
						

						// calcul de la valeur env[i]*son[i]*amp[i]
						val = x->x_env[i]*val*amp; 
						
						out1[j] += val;
						
					}
				else		// non interp
					for(j= dsp_i_begin; j < dsp_i_end; j++)
					{
					
						// Lecture de l'enveloppe
						buffer_index = base_lindex_env + interp_get_int( lindex_env );
		      			x->x_env[i] = t_envsamples[buffer_index];
		      			
		      			lindex_env += lincr_env;
		      			
						
						// Calcul de la forme d'onde
						val = coef * mem1 - mem2;
						mem2 = mem1;
						mem1 = val;		
						

						// calcul de la valeur env[i]*son[i]*amp[i]
						val = x->x_env[i]*val*amp; 
				
						out1[j] += val;
				
					}
			
				
				//********************************
				//  MAJ de l'�tat des grains
				
				
				x->x_ind[i] += nech_process;
				x->Vmem1[i] = mem1;
				x->Vmem2[i] = mem2;
				
				if( (x->x_remain_ind[i] -= nech_process) <= 0)
				{	
					x->x_voiceOn[i] = 0;
					//post("remain kill");
				}
				
				// decremente delai
				x->x_delay[i] = MAX(x->x_delay[i] - N,0);
					
					

			} else { 
				
				//sinon = si la voix est libre// 
	   			x->x_voiceOn[i] = 0;		
			}
			 
			next:
			j=0; //DUMMY
			   
		} // fin for
   		   		  
		x->x_sigin = sigin;

		return (w+11);

	}

	zero:
		// Pas de declenchement de grains
		while (n--)
		{
    		*out1++ = 0.0;
		}
	
    out:    
		return (w+11);
}
//***********************************************************************************//

//-----------------------------------------------------------------------------------//

t_int *synGranul_perform2(t_int *w)
{   
	t_synGranul *x = (t_synGranul *)(w[1]);

    t_float * in			= (float *)(w[2]);
	t_float * t_freq 		= (float *)(w[3]);
    t_float * t_phase	 	= (float *)(w[4]);
    t_float * t_amp			= (float *)(w[5]);
    t_float * t_length 		= (float *)(w[6]);
    t_float * t_pan 		= (float *)(w[7]);
    t_float * t_dist 		= (float *)(w[8]);

    t_float *out1 = (t_float *)(w[9]);
    t_float *out2 = (t_float *)(w[10]);
      
    int n = (int)(w[11]);

	int t_buf;

    int N = n;
    int xn = 0, p;

	double srms = x->x_sr*0.001; // double for precision
	double indT;
	float val;
	float val1,val2;
    float sigin, prevsigin,savein; // values of actual and previous input sample   
	
	float fn,iphase;
	
	float lengthind = x->x_length*srms;	
	
	// temporary buffer index variable
	int t_active_env;
	float * t_envsamples;

    int i,j;
	int nvoices = x->x_nvoices;
	
	int initTest;
	
	//variables grains
	
	float mem1,mem2;
	float coef;
	float amp;
	
	//variables boucles dsp
	
	int dsp_i_begin;
	int dsp_i_end;
	
	int nech_process;
	
	// interp variables
	
	long base_lindex_env;
	long lindex_env;
	long lincr_env;
	
	int buffer_index;
	int buffer_index_plus_1;
	int interp_table_index;
	
	t_linear_interp * interp_table = x->x_linear_interp_table;

	// debug var
#ifdef PERF_DEBUG
    int n_voices_active;
    
#endif

	sigin = x->x_sigin;
   	
#ifdef PERF_DEBUG
	if(ask_poll)
	{
		n_voices_active=0;
	
		for(i=0; i< nvoices; i++) if (x->x_voiceOn[i] ) n_voices_active++;
		
		SETSYM( info_list, gensym("voices"));
		SETLONG(info_list+1, n_voices_active); 
		outlet_list(info,0l,2,info_list);
	}
#endif

	initTest = synGranul_bufferinfos(x);
   	if(initTest == 0) goto zero;
	else
	{
		t_active_env = bufferenv_check(x,x->x_active_env);
		// %%%%%%%%%%     creation de grains   %%%%%%%%%%%%%%%%%
   			
   		p=nvoices;
   		
		// Dans le cas d'un grain d�clench� par un bang
		savein = in[0];
   		if(x->x_askfor)
   		{
			sigin = -1.;
			in[0] = 1.;
			x->x_askfor = 0;
   		}
		
		// Meme chose avec declenchement par zero-crossing sur tout le vecteur in, dans la limite fixee par nvoices
		while (n-- && p)
		{
			//-----signal zero-crossing ascendant declenche un grain----//
			prevsigin = sigin;
			sigin = in[xn];

			if (prevsigin <= 0 && sigin > 0) // creation d'un grain
			{
				while(--p && x->x_voiceOn[p] ) { }  // avance jusqu a la 1ere voix libre
				
				if(p)
				{
						// affecte les valeurs au grain
					x->x_voiceOn[p] = 1;														// activation
					x->Vfreq[p] = (x->x_in2con) * t_freq[xn] + (1 - x->x_in2con) * x->x_freq;						// frequence 
					x->Vphase[p] = (x->x_in3con) ? MOD(t_phase[xn],1.) : x->x_phase;			// phase
					x->Vphase_inc[p] = x->Vfreq[p] / x->x_sr;									// increment de phase
					
						// MAGIC CIRCLE
					fn = x->Vfreq[p]* TWOPI / x->x_sr;
					iphase = x->Vphase[p] * TWOPI;
					x->Vcoef[p] = 2. * cos(fn);
					x->Vmem1[p] = sin(iphase-fn);
					x->Vmem2[p] = sin(iphase-2*fn);
					
					
					x->Vamp[p]		= (x->x_in4con) * t_amp[xn] + (1 - x->x_in4con) * x->x_amp;						// amplitude
					x->Venv[p] = t_active_env;												// enveloppe active pour ce grain
					
					if(x->x_in5con)
					{
						if(t_length[xn]<0)
						{	
							x->Vlength[p]	= -t_length[xn]*srms;
							x->envinc[p]	= -1.*(float)(x->x_env_frames[t_active_env] - 1.) / x->Vlength[p] ;
							x->envind[p]	= x->x_env_frames[t_active_env] - 1;
						}
						else
						{
							x->Vlength[p]	= t_length[xn]*srms;
							x->envinc[p]	= (float)(x->x_env_frames[t_active_env] - 1.) / x->Vlength[p] ;
							x->envind[p]	= 0. ;
						}
					}
					else
					{
						x->Vlength[p] = lengthind;
						x->envinc[p] = x->x_env_dir * x->x_env_frames[t_active_env] / x->Vlength[p] ;
						if(x->x_env_dir < 0)
							x->envind[p] =  x->x_env_frames[t_active_env] - 1;
						else 
							x->envind[p] = 0.;
					}

					x->Vpan[p]		= (x->x_in6con) * t_pan[xn] + (1 - x->x_in6con) * x->x_pan;						// pan
					x->Vdist[p]		= (x->x_in7con) * t_dist[xn] + (1 - x->x_in7con) * x->x_dist;					// distance
					synGranul_pannerV(x,p);
          
					x->x_ind[p] = 0;
					x->x_remain_ind[p] = (long) x->Vlength[p];
					x->x_delay[p] = xn;   // delay de declenchement dans le vecteur         	  
    					
				} else goto perform;
			}
			xn++ ; // boucle on incremente le pointeur dans le vecteur in et le delay de declenchement
		}
   		 
		// %%%%%%%%%%     fin creation     %%%%%%%%%%%%%%%%%
   		
   		perform :
		in[0] = savein;
		
   		//&&&&&&&&&&&&&  Boucle DSP  &&&&&&&&&&&&&
   		n = N;
   		j=0;
		while (n--)
		{
			out1[j] = 0;
			out2[j] = 0;

			j++;   
		}	
    		 	         		   				 									 
		for (i=0; i < nvoices; i++)
		{         
			//si la voix est en cours              
			if (x->x_voiceOn[i]) //&& x->x_ind[i] < x->Vlength[i] )
			{
				// si delay + grand que taille vecteur on passe � voix suivante 
				if(x->x_delay[i] >= N)
				{	
					x->x_delay[i] -= N ;
					goto next;
				}
				
				// nb d'ech a caluler pour ce vecteur
				nech_process = MIN( (N - x->x_delay[i]) , x->x_remain_ind[i] );
				
				
				dsp_i_begin = x->x_delay[i];
				dsp_i_end = x->x_delay[i] + nech_process;
				
				t_envsamples = x->x_env_samples[x->Venv[i]];
				
				// Enveloppe long index & incr calcul
				base_lindex_env = (long) x->envind[i];
				lindex_env = interp_index_scale( x->envind[i] - (double) base_lindex_env );
				lincr_env  = interp_index_scale(x->envinc[i]);
				
				x->envind[i] += nech_process * x->envinc[i];
				
				// Init des memoires algo sinus
				coef = x->Vcoef[i];
				mem1 = x->Vmem1[i];
				mem2 = x->Vmem2[i];
				
				amp = x->Vamp[i];
				
				
				//***********************************
				// CALCUL EFFECTIF DES ECHANTILLONS
				//***********************************
				
				if( x->x_sinterp == 1 ) // interp 
					for(j= dsp_i_begin; j < dsp_i_end; j++)
					{
					
						// Lecture de l'enveloppe
						buffer_index = base_lindex_env + interp_get_int( lindex_env );
						interp_table_index = interp_get_table_index( lindex_env );
		      			x->x_env[i] = interp_table[interp_table_index].a * t_envsamples[buffer_index]
		      							+ interp_table[interp_table_index].b * t_envsamples[buffer_index + 1];
		      			
		      			lindex_env += lincr_env;
		      			
						// Calcul de la forme d'onde
						val = coef * mem1 - mem2;
						mem2 = mem1;
						mem1 = val;			
						

						// calcul de la valeur env[i]*son[i]*amp[i]
						val = x->x_env[i]*val*amp; 

						// calcul du pan 
						val1 = x->Vhp1[i]*val;
						val2 = x->Vhp2[i]*val; 

						
						out1[j] += val1;
						out2[j] += val2;

						
						
					}
				else		// non interp
					for(j= dsp_i_begin; j < dsp_i_end; j++)
					{
					
						// Lecture de l'enveloppe
						buffer_index = base_lindex_env + interp_get_int( lindex_env );
		      			x->x_env[i] = t_envsamples[buffer_index];
		      			
		      			lindex_env += lincr_env;
		      			
						
						// Calcul de la forme d'onde
						val = coef * mem1 - mem2;
						mem2 = mem1;
						mem1 = val;		
						

						// calcul de la valeur env[i]*son[i]*amp[i]
						val = x->x_env[i]*val*amp; 

						// calcul du pan 
						val1 = x->Vhp1[i]*val;
						val2 = x->Vhp2[i]*val; 

						
						out1[j] += val1;
						out2[j] += val2;

						
					}
			
				
				//********************************
				//  MAJ de l'�tat des grains
				
				
				x->x_ind[i] += nech_process;
				x->Vmem1[i] = mem1;
				x->Vmem2[i] = mem2;
				
				if( (x->x_remain_ind[i] -= nech_process) <= 0)
				{	
					x->x_voiceOn[i] = 0;
					//post("remain kill");
				}
				
				// decremente delai
				x->x_delay[i] = MAX(x->x_delay[i] - N,0);
					
					

			} else { 
				
				//sinon = si la voix est libre// 
	   			x->x_voiceOn[i] = 0;		
			}
			 
			next:
			j=0; //DUMMY
			   
		} // fin for
   		   		  
		x->x_sigin = sigin;

		return (w+12);

	}

	zero:
		// Pas de declenchement de grains
		while (n--)
		{
    		*out1++ = 0.0;
			*out2++ = 0.0;
		}
	
    out:    
		return (w+12);
}
//***********************************************************************************//

//-----------------------------------------------------------------------------------//
t_int *synGranul_perform4(t_int *w)
{   

	t_synGranul *x = (t_synGranul *)(w[1]);

    t_float * in			= (float *)(w[2]);
	t_float * t_freq 		= (float *)(w[3]);
    t_float * t_phase	 	= (float *)(w[4]);
    t_float * t_amp			= (float *)(w[5]);
    t_float * t_length 		= (float *)(w[6]);
    t_float * t_pan 		= (float *)(w[7]);
    t_float * t_dist 		= (float *)(w[8]);

    t_float *out1 = (t_float *)(w[9]);
    t_float *out2 = (t_float *)(w[10]);
    t_float *out3 = (t_float *)(w[11]);
    t_float *out4 = (t_float *)(w[12]);
	
    int n = (int)(w[13]);

    int N = n;
    int xn = 0, p;
                    
	double srms = x->x_sr*0.001; // double for precision
	double indT;
	float val;
	float val1,val2,val3,val4;
    float sigin, prevsigin,savein; // values of actual and previous input sample   
	
	float fn,iphase;
	
	float lengthind = x->x_length*srms;	
	
	// temporary buffer index variable
	int t_active_env;
	float * t_envsamples;

    int i,j;
	int nvoices = x->x_nvoices;
	
	int initTest;
	
	//variables grains
	
	float mem1,mem2;
	float coef;
	float amp;
	
	//variables boucles dsp
	
	int dsp_i_begin;
	int dsp_i_end;
	
	int nech_process;
	
	// interp variables
	
	long base_lindex_env;
	long lindex_env;
	long lincr_env;
	
	int buffer_index;
	int buffer_index_plus_1;
	int interp_table_index;
	
	t_linear_interp * interp_table = x->x_linear_interp_table;

	// debug var
#ifdef PERF_DEBUG
    int n_voices_active;
    
#endif

	sigin = x->x_sigin;
   	
#ifdef PERF_DEBUG
	if(ask_poll)
	{
		n_voices_active=0;
	
		for(i=0; i< nvoices; i++) if (x->x_voiceOn[i] ) n_voices_active++;
		
		SETSYM( info_list, gensym("voices"));
		SETLONG(info_list+1, n_voices_active); 
		outlet_list(info,0l,2,info_list);
	}
#endif

	initTest = synGranul_bufferinfos(x);
   	if(initTest == 0) goto zero;
	else
	{
		t_active_env = bufferenv_check(x,x->x_active_env);
		// %%%%%%%%%%     creation de grains   %%%%%%%%%%%%%%%%%
   			
   		p=nvoices;
   		
		// Dans le cas d'un grain d�clench� par un bang
		savein = in[0];
   		if(x->x_askfor)
   		{
			sigin = -1.;
			in[0] = 1.;
			x->x_askfor = 0;
   		}
		
		// Meme chose avec declenchement par zero-crossing sur tout le vecteur in, dans la limite fixee par nvoices
		while (n-- && p)
		{
			//-----signal zero-crossing ascendant declenche un grain----//
			prevsigin = sigin;
			sigin = in[xn];

			if (prevsigin <= 0 && sigin > 0) // creation d'un grain
			{
				while(--p && x->x_voiceOn[p] ) { }  // avance jusqu a la 1ere voix libre
				
				if(p)
				{
						// affecte les valeurs au grain
					x->x_voiceOn[p] = 1;														// activation
					x->Vfreq[p] = (x->x_in2con) * t_freq[xn] + (1 - x->x_in2con) * x->x_freq;						// frequence 
					x->Vphase[p] = (x->x_in3con) ? MOD(t_phase[xn],1.) : x->x_phase;			// phase
					x->Vphase_inc[p] = x->Vfreq[p] / x->x_sr;									// increment de phase
					
						// MAGIC CIRCLE
					fn = x->Vfreq[p]* TWOPI / x->x_sr;
					iphase = x->Vphase[p] * TWOPI;
					x->Vcoef[p] = 2. * cos(fn);
					x->Vmem1[p] = sin(iphase-fn);
					x->Vmem2[p] = sin(iphase-2*fn);
					
					
					x->Vamp[p]		= (x->x_in4con) * t_amp[xn] + (1 - x->x_in4con) * x->x_amp;						// amplitude
					x->Venv[p] = t_active_env;												// enveloppe active pour ce grain
					
					if(x->x_in5con)
					{
						if(t_length[xn]<0)
						{	
							x->Vlength[p]	= -t_length[xn]*srms;
							x->envinc[p]	= -1.*(float)(x->x_env_frames[t_active_env] - 1.) / x->Vlength[p] ;
							x->envind[p]	= x->x_env_frames[t_active_env] - 1;
						}
						else
						{
							x->Vlength[p]	= t_length[xn]*srms;
							x->envinc[p]	= (float)(x->x_env_frames[t_active_env] - 1.) / x->Vlength[p] ;
							x->envind[p]	= 0. ;
						}
					}
					else
					{
						x->Vlength[p] = lengthind;
						x->envinc[p] = x->x_env_dir * x->x_env_frames[t_active_env] / x->Vlength[p] ;
						if(x->x_env_dir < 0)
							x->envind[p] =  x->x_env_frames[t_active_env] - 1;
						else 
							x->envind[p] = 0.;
					}

					x->Vpan[p]		= (x->x_in6con) * t_pan[xn] + (1 - x->x_in6con) * x->x_pan;						// pan
					x->Vdist[p]		= (x->x_in7con) * t_dist[xn] + (1 - x->x_in7con) * x->x_dist;					// distance
					synGranul_pannerV(x,p);
          
					x->x_ind[p] = 0;
					x->x_remain_ind[p] = (long) x->Vlength[p];
					x->x_delay[p] = xn;   // delay de declenchement dans le vecteur         	  
    					
				} else goto perform;
			}
			xn++ ; // boucle on incremente le pointeur dans le vecteur in et le delay de declenchement
		}
   		 
		// %%%%%%%%%%     fin creation     %%%%%%%%%%%%%%%%%
   		
   		perform :
		in[0] = savein;
		
   		//&&&&&&&&&&&&&  Boucle DSP  &&&&&&&&&&&&&
   		n = N;
   		j=0;
		while (n--)
		{
			out1[j] = 0;
			out2[j] = 0;
			out3[j] = 0;
			out4[j] = 0;
			j++;   
		}	
    		 	         		   				 									 
		for (i=0; i < nvoices; i++)
		{         
			//si la voix est en cours              
			if (x->x_voiceOn[i]) //&& x->x_ind[i] < x->Vlength[i] )
			{
				// si delay + grand que taille vecteur on passe � voix suivante 
				if(x->x_delay[i] >= N)
				{	
					x->x_delay[i] -= N ;
					goto next;
				}
				
				// nb d'ech a caluler pour ce vecteur
				nech_process = MIN( (N - x->x_delay[i]) , x->x_remain_ind[i] );
				
				
				dsp_i_begin = x->x_delay[i];
				dsp_i_end = x->x_delay[i] + nech_process;
				
				t_envsamples = x->x_env_samples[x->Venv[i]];
				
				// Enveloppe long index & incr calcul
				base_lindex_env = (long) x->envind[i];
				lindex_env = interp_index_scale( x->envind[i] - (double) base_lindex_env );
				lincr_env  = interp_index_scale(x->envinc[i]);
				
				x->envind[i] += nech_process * x->envinc[i];
				
				// Init des memoires algo sinus
				coef = x->Vcoef[i];
				mem1 = x->Vmem1[i];
				mem2 = x->Vmem2[i];
				
				amp = x->Vamp[i];
				
				
				//***********************************
				// CALCUL EFFECTIF DES ECHANTILLONS
				//***********************************
				
				if( x->x_sinterp == 1 ) // interp 
					for(j= dsp_i_begin; j < dsp_i_end; j++)
					{
					
						// Lecture de l'enveloppe
						buffer_index = base_lindex_env + interp_get_int( lindex_env );
						interp_table_index = interp_get_table_index( lindex_env );
		      			x->x_env[i] = interp_table[interp_table_index].a * t_envsamples[buffer_index]
		      							+ interp_table[interp_table_index].b * t_envsamples[buffer_index + 1];
		      			
		      			lindex_env += lincr_env;
		      			
						// Calcul de la forme d'onde
						val = coef * mem1 - mem2;
						mem2 = mem1;
						mem1 = val;			
						

						// calcul de la valeur env[i]*son[i]*amp[i]
						val = x->x_env[i]*val*amp; 

						// calcul du pan 
						val1 = x->Vhp1[i]*val;
						val2 = x->Vhp2[i]*val; 
						val3 = x->Vhp3[i]*val;
						val4 = x->Vhp4[i]*val;
						
						out1[j] += val1;
						out2[j] += val2;
						out3[j] += val3;
						out4[j] += val4;
						
						
					}
				else		// non interp
					for(j= dsp_i_begin; j < dsp_i_end; j++)
					{
					
						// Lecture de l'enveloppe
						buffer_index = base_lindex_env + interp_get_int( lindex_env );
		      			x->x_env[i] = t_envsamples[buffer_index];
		      			
		      			lindex_env += lincr_env;
		      			
						
						// Calcul de la forme d'onde
						val = coef * mem1 - mem2;
						mem2 = mem1;
						mem1 = val;		
						

						// calcul de la valeur env[i]*son[i]*amp[i]
						val = x->x_env[i]*val*amp; 

						// calcul du pan 
						val1 = x->Vhp1[i]*val;
						val2 = x->Vhp2[i]*val; 
						val3 = x->Vhp3[i]*val;
						val4 = x->Vhp4[i]*val;
						
						out1[j] += val1;
						out2[j] += val2;
						out3[j] += val3;
						out4[j] += val4;
						
					}
			
				
				//********************************
				//  MAJ de l'�tat des grains
				
				
				x->x_ind[i] += nech_process;
				x->Vmem1[i] = mem1;
				x->Vmem2[i] = mem2;
				
				if( (x->x_remain_ind[i] -= nech_process) <= 0)
				{	
					x->x_voiceOn[i] = 0;
					//post("remain kill");
				}
				
				// decremente delai
				x->x_delay[i] = MAX(x->x_delay[i] - N,0);
					
					

			} else { 
				
				//sinon = si la voix est libre// 
	   			x->x_voiceOn[i] = 0;		
			}
			 
			next:
			j=0; //DUMMY
			   
		} // fin for
        		  
		x->x_sigin = sigin;
    		
		return (w+14);

	}

    zero:
		// Pas de declenchement de grains
		while (n--)
		{
    		*out1++ = 0.0;
			*out2++ = 0.0;
			*out3++ = 0.0;
			*out4++ = 0.0; 
		}
	
    out:  
		return (w+14);
}
//***********************************************************************************//

t_int *synGranul_perform6(t_int *w)
{   
	t_synGranul *x = (t_synGranul *)(w[1]);

    t_float * in			= (float *)(w[2]);
	t_float * t_freq 		= (float *)(w[3]);
    t_float * t_phase 	= (float *)(w[4]);
    t_float * t_amp			= (float *)(w[5]);
    t_float * t_length 		= (float *)(w[6]);
    t_float * t_pan 		= (float *)(w[7]);
    t_float * t_dist 		= (float *)(w[8]);

    t_float *out1 = (t_float *)(w[9]);
    t_float *out2 = (t_float *)(w[10]);
    t_float *out3 = (t_float *)(w[11]);
    t_float *out4 = (t_float *)(w[12]);
	t_float *out5 = (t_float *)(w[13]);
    t_float *out6 = (t_float *)(w[14]);
      
    int n = (int)(w[15]);

    int N = n;
    int xn = 0, p;
    
	double srms = x->x_sr*0.001; // double for precision
	double indT;
	float  val;
	float val1,val2,val3,val4,val5,val6;
    float sigin, prevsigin,savein; // values of actual and previous input sample   
	
	float fn,iphase;
	
	float lengthind = x->x_length*srms;	
	
	// temporary buffer index variable
	int t_active_env;
	float * t_envsamples;

    int i,j;
	int nvoices = x->x_nvoices;
	
	int initTest;
	
	//variables grains
	
	float mem1,mem2;
	float coef;
	float amp;
	
	//variables boucles dsp
	
	int dsp_i_begin;
	int dsp_i_end;
	
	int nech_process;
	
	// interp variables
	
	long base_lindex_env;
	long lindex_env;
	long lincr_env;
	
	int buffer_index;
	int buffer_index_plus_1;
	int interp_table_index;
	
	t_linear_interp * interp_table = x->x_linear_interp_table;

	// debug var
#ifdef PERF_DEBUG
    int n_voices_active;
    
#endif

	sigin = x->x_sigin;
   	
#ifdef PERF_DEBUG
	if(ask_poll)
	{
		n_voices_active=0;
	
		for(i=0; i< nvoices; i++) if (x->x_voiceOn[i] ) n_voices_active++;
		
		SETSYM( info_list, gensym("voices"));
		SETLONG(info_list+1, n_voices_active); 
		outlet_list(info,0l,2,info_list);
	}
#endif

	initTest = synGranul_bufferinfos(x);
   	if(initTest == 0) goto zero;
	else
	{
		t_active_env = bufferenv_check(x,x->x_active_env);
		// %%%%%%%%%%     creation de grains   %%%%%%%%%%%%%%%%%
   			
   		p=nvoices;
   		
		// Dans le cas d'un grain d�clench� par un bang
		savein = in[0];
   		if(x->x_askfor)
   		{
			sigin = -1.;
			in[0] = 1.;
			x->x_askfor = 0;
   		}
		
		// Meme chose avec declenchement par zero-crossing sur tout le vecteur in, dans la limite fixee par nvoices
		while (n-- && p)
		{
			//-----signal zero-crossing ascendant declenche un grain----//
			prevsigin = sigin;
			sigin = in[xn];

			if (prevsigin <= 0 && sigin > 0) // creation d'un grain
			{
				while(--p && x->x_voiceOn[p] ) { }  // avance jusqu a la 1ere voix libre
				
				if(p)
				{
						// affecte les valeurs au grain
					x->x_voiceOn[p] = 1;														// activation
					x->Vfreq[p] = (x->x_in2con) * t_freq[xn] + (1 - x->x_in2con) * x->x_freq;						// frequence 
					x->Vphase[p] = (x->x_in3con) ? MOD(t_phase[xn],1.) : x->x_phase;			// phase
					x->Vphase_inc[p] = x->Vfreq[p] / x->x_sr;									// increment de phase
					
						// MAGIC CIRCLE
					fn = x->Vfreq[p]* TWOPI / x->x_sr;
					iphase = x->Vphase[p] * TWOPI;
					x->Vcoef[p] = 2. * cos(fn);
					x->Vmem1[p] = sin(iphase-fn);
					x->Vmem2[p] = sin(iphase-2*fn);
					
					
					x->Vamp[p]		= (x->x_in4con) * t_amp[xn] + (1 - x->x_in4con) * x->x_amp;						// amplitude
					x->Venv[p] = t_active_env;												// enveloppe active pour ce grain
					
					if(x->x_in5con)
					{
						if(t_length[xn]<0)
						{	
							x->Vlength[p]	= -t_length[xn]*srms;
							x->envinc[p]	= -1.*(float)(x->x_env_frames[t_active_env] - 1.) / x->Vlength[p] ;
							x->envind[p]	= x->x_env_frames[t_active_env] - 1;
						}
						else
						{
							x->Vlength[p]	= t_length[xn]*srms;
							x->envinc[p]	= (float)(x->x_env_frames[t_active_env] - 1.) / x->Vlength[p] ;
							x->envind[p]	= 0. ;
						}
					}
					else
					{
						x->Vlength[p] = lengthind;
						x->envinc[p] = x->x_env_dir * x->x_env_frames[t_active_env] / x->Vlength[p] ;
						if(x->x_env_dir < 0)
							x->envind[p] =  x->x_env_frames[t_active_env] - 1;
						else 
							x->envind[p] = 0.;
					}

					x->Vpan[p]		= (x->x_in6con) * t_pan[xn] + (1 - x->x_in6con) * x->x_pan;						// pan
					x->Vdist[p]		= (x->x_in7con) * t_dist[xn] + (1 - x->x_in7con) * x->x_dist;					// distance
					synGranul_pannerV(x,p);
          
					x->x_ind[p] = 0;
					x->x_remain_ind[p] = (long) x->Vlength[p];
					x->x_delay[p] = xn;   // delay de declenchement dans le vecteur         	  
    					
				} else goto perform;
			}
			xn++ ; // boucle on incremente le pointeur dans le vecteur in et le delay de declenchement
		}
   		 
		// %%%%%%%%%%     fin creation     %%%%%%%%%%%%%%%%%
   		
   		perform :
		in[0] = savein;
		
   		//&&&&&&&&&&&&&  Boucle DSP  &&&&&&&&&&&&&
   		n = N;
   		j=0;
		while (n--)
		{
			out1[j] = 0;
			out2[j] = 0;
			out3[j] = 0;
			out4[j] = 0;
			out5[j] = 0;
			out6[j] = 0;
			j++;   
		}	
    		 	         		   				 									 
		for (i=0; i < nvoices; i++)
		{         
			//si la voix est en cours              
			if (x->x_voiceOn[i]) //&& x->x_ind[i] < x->Vlength[i] )
			{
				// si delay + grand que taille vecteur on passe � voix suivante 
				if(x->x_delay[i] >= N)
				{	
					x->x_delay[i] -= N ;
					goto next;
				}
				
				// nb d'ech a caluler pour ce vecteur
				nech_process = MIN( (N - x->x_delay[i]) , x->x_remain_ind[i] );
				
				
				dsp_i_begin = x->x_delay[i];
				dsp_i_end = x->x_delay[i] + nech_process;
				
				t_envsamples = x->x_env_samples[x->Venv[i]];
				
				// Enveloppe long index & incr calcul
				base_lindex_env = (long) x->envind[i];
				lindex_env = interp_index_scale( x->envind[i] - (double) base_lindex_env );
				lincr_env  = interp_index_scale(x->envinc[i]);
				
				x->envind[i] += nech_process * x->envinc[i];
				
				// Init des memoires algo sinus
				coef = x->Vcoef[i];
				mem1 = x->Vmem1[i];
				mem2 = x->Vmem2[i];
				
				amp = x->Vamp[i];
				
				
				//***********************************
				// CALCUL EFFECTIF DES ECHANTILLONS
				//***********************************
				
				if( x->x_sinterp == 1 ) // interp 
					for(j= dsp_i_begin; j < dsp_i_end; j++)
					{
					
						// Lecture de l'enveloppe
						buffer_index = base_lindex_env + interp_get_int( lindex_env );
						interp_table_index = interp_get_table_index( lindex_env );
		      			x->x_env[i] = interp_table[interp_table_index].a * t_envsamples[buffer_index]
		      							+ interp_table[interp_table_index].b * t_envsamples[buffer_index + 1];
		      			
		      			lindex_env += lincr_env;
		      			
						// Calcul de la forme d'onde
						val = coef * mem1 - mem2;
						mem2 = mem1;
						mem1 = val;			
						

						// calcul de la valeur env[i]*son[i]*amp[i]
						val = x->x_env[i]*val*amp; 

						// calcul du pan 
						val1 = x->Vhp1[i]*val;
						val2 = x->Vhp2[i]*val; 
						val3 = x->Vhp3[i]*val;
						val4 = x->Vhp4[i]*val;
						val5 = x->Vhp5[i]*val;
						val6 = x->Vhp6[i]*val;
						
						out1[j] += val1;
						out2[j] += val2;
						out3[j] += val3;
						out4[j] += val4;
						out5[j] += val5;
						out6[j] += val6;
						
						
					}
				else		// non interp
					for(j= dsp_i_begin; j < dsp_i_end; j++)
					{
					
						// Lecture de l'enveloppe
						buffer_index = base_lindex_env + interp_get_int( lindex_env );
		      			x->x_env[i] = t_envsamples[buffer_index];
		      			
		      			lindex_env += lincr_env;
		      			
						
						// Calcul de la forme d'onde
						val = coef * mem1 - mem2;
						mem2 = mem1;
						mem1 = val;		
						

						// calcul de la valeur env[i]*son[i]*amp[i]
						val = x->x_env[i]*val*amp; 

						// calcul du pan 
						val1 = x->Vhp1[i]*val;
						val2 = x->Vhp2[i]*val; 
						val3 = x->Vhp3[i]*val;
						val4 = x->Vhp4[i]*val;
						val5 = x->Vhp5[i]*val;
						val6 = x->Vhp6[i]*val;
						
						out1[j] += val1;
						out2[j] += val2;
						out3[j] += val3;
						out4[j] += val4;
						out5[j] += val5;
						out6[j] += val6;
						
					}
			
				
				//********************************
				//  MAJ de l'�tat des grains
				
				
				x->x_ind[i] += nech_process;
				x->Vmem1[i] = mem1;
				x->Vmem2[i] = mem2;
				
				if( (x->x_remain_ind[i] -= nech_process) <= 0)
				{	
					x->x_voiceOn[i] = 0;
					//post("remain kill");
				}
				
				// decremente delai
				x->x_delay[i] = MAX(x->x_delay[i] - N,0);
					
					

			} else { 
				
				//sinon = si la voix est libre// 
	   			x->x_voiceOn[i] = 0;		
			}
			 
			next:
			j=0; //DUMMY
			   
		} // fin for
        		  
		x->x_sigin = sigin;

		return (w+16);

	}

	zero:
		// Pas de declenchement de grains
		while (n--)
		{
    		*out1++ = 0.0;
			*out2++ = 0.0;
			*out3++ = 0.0;
			*out4++ = 0.0;
    		*out5++ = 0.0;
			*out6++ = 0.0;
		}
	
    out:    
		return (w+16);
}

//***********************************************************************************//

t_int *synGranul_perform8(t_int *w)
{   
	t_synGranul *x = (t_synGranul *)(w[1]);

    t_float * in			= (float *)(w[2]);
	t_float * t_freq 		= (float *)(w[3]);
    t_float * t_phase	 	= (float *)(w[4]);
    t_float * t_amp			= (float *)(w[5]);
    t_float * t_length 		= (float *)(w[6]);
    t_float * t_pan 		= (float *)(w[7]);
    t_float * t_dist 		= (float *)(w[8]);

    t_float *out1 = (t_float *)(w[9]);
    t_float *out2 = (t_float *)(w[10]);
    t_float *out3 = (t_float *)(w[11]);
    t_float *out4 = (t_float *)(w[12]);
	t_float *out5 = (t_float *)(w[13]);
    t_float *out6 = (t_float *)(w[14]);
	t_float *out7 = (t_float *)(w[15]);
    t_float *out8 = (t_float *)(w[16]);
      
    int n = (int)(w[17]);

    int N = n;
    int xn = 0, p;
    
	double srms = x->x_sr*0.001; // double for precision
	double indT;
	float val;
	float val1,val2,val3,val4,val5,val6,val7,val8;
    float sigin, prevsigin,savein; // values of actual and previous input sample   
	
	float fn,iphase;
	
	float lengthind = x->x_length*srms;	
	
	// temporary buffer index variable
	int t_active_env;
	float * t_envsamples;

    int i,j;
	int nvoices = x->x_nvoices;
	
	int initTest;
	
	//variables grains
	
	float mem1,mem2;
	float coef;
	float amp;
	
	//variables boucles dsp
	
	int dsp_i_begin;
	int dsp_i_end;
	
	int nech_process;
	
	// interp variables
	
	long base_lindex_env;
	long lindex_env;
	long lincr_env;
	
	int buffer_index;
	int buffer_index_plus_1;
	int interp_table_index;
	
	t_linear_interp * interp_table = x->x_linear_interp_table;

	// debug var
#ifdef PERF_DEBUG
    int n_voices_active;
    
#endif

	sigin = x->x_sigin;
   	
#ifdef PERF_DEBUG
	if(ask_poll)
	{
		n_voices_active=0;
	
		for(i=0; i< nvoices; i++) if (x->x_voiceOn[i] ) n_voices_active++;
		
		SETSYM( info_list, gensym("voices"));
		SETLONG(info_list+1, n_voices_active); 
		outlet_list(info,0l,2,info_list);
	}
#endif

	initTest = synGranul_bufferinfos(x);
   	if(initTest == 0) goto zero;
	else
	{
		t_active_env = bufferenv_check(x,x->x_active_env);
		// %%%%%%%%%%     creation de grains   %%%%%%%%%%%%%%%%%
   			
   		p=nvoices;
   		
		// Dans le cas d'un grain d�clench� par un bang
		savein = in[0];
   		if(x->x_askfor)
   		{
			sigin = -1.;
			in[0] = 1.;
			x->x_askfor = 0;
   		}
		
		// Meme chose avec declenchement par zero-crossing sur tout le vecteur in, dans la limite fixee par nvoices
		while (n-- && p)
		{
			//-----signal zero-crossing ascendant declenche un grain----//
			prevsigin = sigin;
			sigin = in[xn];

			if (prevsigin <= 0 && sigin > 0) // creation d'un grain
			{
				while(--p && x->x_voiceOn[p] ) { }  // avance jusqu a la 1ere voix libre
				
				if(p)
				{
						// affecte les valeurs au grain
					x->x_voiceOn[p] = 1;														// activation
					x->Vfreq[p] = (x->x_in2con) * t_freq[xn] + (1 - x->x_in2con) * x->x_freq;						// frequence 
					x->Vphase[p] = (x->x_in3con) ? MOD(t_phase[xn],1.) : x->x_phase;			// phase
					x->Vphase_inc[p] = x->Vfreq[p] / x->x_sr;									// increment de phase
					
						// MAGIC CIRCLE
					fn = x->Vfreq[p]* TWOPI / x->x_sr;
					iphase = x->Vphase[p] * TWOPI;
					x->Vcoef[p] = 2. * cos(fn);
					x->Vmem1[p] = sin(iphase-fn);
					x->Vmem2[p] = sin(iphase-2*fn);
					
					
					x->Vamp[p]		= (x->x_in4con) * t_amp[xn] + (1 - x->x_in4con) * x->x_amp;						// amplitude
					x->Venv[p] = t_active_env;												// enveloppe active pour ce grain
					
					if(x->x_in5con)
					{
						if(t_length[xn]<0)
						{	
							x->Vlength[p]	= -t_length[xn]*srms;
							x->envinc[p]	= -1.*(float)(x->x_env_frames[t_active_env] - 1.) / x->Vlength[p] ;
							x->envind[p]	= x->x_env_frames[t_active_env] - 1;
						}
						else
						{
							x->Vlength[p]	= t_length[xn]*srms;
							x->envinc[p]	= (float)(x->x_env_frames[t_active_env] - 1.) / x->Vlength[p] ;
							x->envind[p]	= 0. ;
						}
					}
					else
					{
						x->Vlength[p] = lengthind;
						x->envinc[p] = x->x_env_dir * x->x_env_frames[t_active_env] / x->Vlength[p] ;
						if(x->x_env_dir < 0)
							x->envind[p] =  x->x_env_frames[t_active_env] - 1;
						else 
							x->envind[p] = 0.;
					}

					x->Vpan[p]		= (x->x_in6con) * t_pan[xn] + (1 - x->x_in6con) * x->x_pan;						// pan
					x->Vdist[p]		= (x->x_in7con) * t_dist[xn] + (1 - x->x_in7con) * x->x_dist;					// distance
					synGranul_pannerV(x,p);
          
					x->x_ind[p] = 0;
					x->x_remain_ind[p] = (long) x->Vlength[p];
					x->x_delay[p] = xn;   // delay de declenchement dans le vecteur         	  
    					
				} else goto perform;
			}
			xn++ ; // boucle on incremente le pointeur dans le vecteur in et le delay de declenchement
		}
   		 
		// %%%%%%%%%%     fin creation     %%%%%%%%%%%%%%%%%
   		
   		perform :
		in[0] = savein;
		
   		//&&&&&&&&&&&&&  Boucle DSP  &&&&&&&&&&&&&
   		n = N;
   		j=0;
		while (n--)
		{
			out1[j] = 0;
			out2[j] = 0;
			out3[j] = 0;
			out4[j] = 0;
			out5[j] = 0;
			out6[j] = 0;
			out7[j] = 0;
			out8[j] = 0;
			j++;   
		}	
    		 	         		   				 									 
		for (i=0; i < nvoices; i++)
		{         
			//si la voix est en cours              
			if (x->x_voiceOn[i]) //&& x->x_ind[i] < x->Vlength[i] )
			{
				// si delay + grand que taille vecteur on passe � voix suivante 
				if(x->x_delay[i] >= N)
				{	
					x->x_delay[i] -= N ;
					goto next;
				}
				
				// nb d'ech a caluler pour ce vecteur
				nech_process = MIN( (N - x->x_delay[i]) , x->x_remain_ind[i] );
				
				
				dsp_i_begin = x->x_delay[i];
				dsp_i_end = x->x_delay[i] + nech_process;
				
				t_envsamples = x->x_env_samples[x->Venv[i]];
				
				// Enveloppe long index & incr calcul
				base_lindex_env = (long) x->envind[i];
				lindex_env = interp_index_scale( x->envind[i] - (double) base_lindex_env );
				lincr_env  = interp_index_scale(x->envinc[i]);
				
				x->envind[i] += nech_process * x->envinc[i];
				
				// Init des memoires algo sinus
				coef = x->Vcoef[i];
				mem1 = x->Vmem1[i];
				mem2 = x->Vmem2[i];
				
				amp = x->Vamp[i];
				
				
				//***********************************
				// CALCUL EFFECTIF DES ECHANTILLONS
				//***********************************
				
				if( x->x_sinterp == 1 ) // interp 
					for(j= dsp_i_begin; j < dsp_i_end; j++)
					{
					
						// Lecture de l'enveloppe
						buffer_index = base_lindex_env + interp_get_int( lindex_env );
						interp_table_index = interp_get_table_index( lindex_env );
		      			x->x_env[i] = interp_table[interp_table_index].a * t_envsamples[buffer_index]
		      							+ interp_table[interp_table_index].b * t_envsamples[buffer_index + 1];
		      			
		      			lindex_env += lincr_env;
		      			
						// Calcul de la forme d'onde
						val = coef * mem1 - mem2;
						mem2 = mem1;
						mem1 = val;			
						

						// calcul de la valeur env[i]*son[i]*amp[i]
						val = x->x_env[i]*val*amp; 

						// calcul du pan 
						val1 = x->Vhp1[i]*val;
						val2 = x->Vhp2[i]*val; 
						val3 = x->Vhp3[i]*val;
						val4 = x->Vhp4[i]*val;
						val5 = x->Vhp5[i]*val;
						val6 = x->Vhp6[i]*val;
						val7 = x->Vhp7[i]*val;
						val8 = x->Vhp8[i]*val;
						
						out1[j] += val1;
						out2[j] += val2;
						out3[j] += val3;
						out4[j] += val4;
						out5[j] += val5;
						out6[j] += val6;
						out7[j] += val7;
						out8[j] += val8;
						
						
					}
				else		// non interp
					for(j= dsp_i_begin; j < dsp_i_end; j++)
					{
					
						// Lecture de l'enveloppe
						buffer_index = base_lindex_env + interp_get_int( lindex_env );
		      			x->x_env[i] = t_envsamples[buffer_index];
		      			
		      			lindex_env += lincr_env;
		      			
						
						// Calcul de la forme d'onde
						val = coef * mem1 - mem2;
						mem2 = mem1;
						mem1 = val;		
						

						// calcul de la valeur env[i]*son[i]*amp[i]
						val = x->x_env[i]*val*amp; 

						// calcul du pan 
						val1 = x->Vhp1[i]*val;
						val2 = x->Vhp2[i]*val; 
						val3 = x->Vhp3[i]*val;
						val4 = x->Vhp4[i]*val;
						val5 = x->Vhp5[i]*val;
						val6 = x->Vhp6[i]*val;
						val7 = x->Vhp7[i]*val;
						val8 = x->Vhp8[i]*val;
						
						out1[j] += val1;
						out2[j] += val2;
						out3[j] += val3;
						out4[j] += val4;
						out5[j] += val5;
						out6[j] += val6;
						out7[j] += val7;
						out8[j] += val8;
						
					}
			
				
				//********************************
				//  MAJ de l'�tat des grains
				
				
				x->x_ind[i] += nech_process;
				x->Vmem1[i] = mem1;
				x->Vmem2[i] = mem2;
				
				if( (x->x_remain_ind[i] -= nech_process) <= 0)
				{	
					x->x_voiceOn[i] = 0;
					//post("remain kill");
				}
				
				// decremente delai
				x->x_delay[i] = MAX(x->x_delay[i] - N,0);
					
					

			} else { 
				
				//sinon = si la voix est libre// 
	   			x->x_voiceOn[i] = 0;		
			}
			 
			next:
			j=0; //DUMMY
			   
		} // fin for
        		  
		x->x_sigin = sigin;

		return (w+18);

	}

	zero:
		// Pas de declenchement de grains
		while (n--)
		{
    		*out1++ = 0.0;
			*out2++ = 0.0;
			*out3++ = 0.0;
			*out4++ = 0.0;
    		*out5++ = 0.0;
			*out6++ = 0.0;
    		*out7++ = 0.0;
			*out8++ = 0.0;
		}
	
    out:    
		return (w+18);
}

//***********************************************************************************//

#ifdef ARCH_PD
int synGranul_bufferinfos(t_synGranul *x)
{
	int cpt;
	t_garray *a;
	t_symbol *s;

	// retourne 0 : sortie nulle
	// retourne 1 : calcul des grains

	for(cpt = 0; cpt < x->x_nenvbuffer ; cpt++)
	{
		if( x->x_env_sym[cpt] ) // si un nom de buffer a �t� donn� -> collecter les infos
		{
		
			if (!(a = (t_garray *)pd_findbyclass(x->x_env_sym[cpt], garray_class)))
			     {
        			 if (x->x_env_sym[cpt]->s_name) pd_error(x, "synGranul~: %s: no such array",
        			     x->x_env_sym[cpt]->s_name);
        			 x->x_env_buf[cpt] = 0;
				 if(cpt == 0) {return 0;}
			     }
			     else if (!garray_getfloatarray(a, &x->x_env_frames[cpt], &x->x_env_samples[cpt]))
			     {
        			 pd_error(x, "%s: bad template for synGranul~", x->x_env_sym[cpt]->s_name);
        			 x->x_env_buf[cpt] = 0;
				 if(cpt == 0) {return 0;}
			     }
			     else 
			     {
				garray_usedindsp(a);
				
				}
			
		}
	}
	
	return 1;
}

#else
int synGranul_bufferinfos(t_synGranul *x)
{
	int cpt;
	t_buffer *b;

	// retourne 0 : sortie nulle
	// retourne 1 : calcul des grains

	// Test si enabled
    if (x->x_obj.z_disabled) return -1;

	// Test sur la validit� et l'activite des buffers & initialisations

	for(cpt = 0; cpt < x->x_nenvbuffer ; cpt++)
	{
		if( x->x_env_sym[cpt] ) // si un nom de buffer a �t� donn� -> collecter les infos
		{
		
			if ((b = (t_buffer *)(x->x_env_sym[cpt]->s_thing)) && ob_sym(b) == ps_buffer && b->b_valid && !b->b_inuse) // attention inuse !!!
			{
				x->x_env_buf[cpt] = b;
				x->x_env_samples[cpt] = b->b_samples;
				x->x_env_frames[cpt] = b->b_frames;
				
				
			}
			else {
				//post("invalid %d",cpt);
				// le buffer 0 est celui utilis� si celui choisi est pas valide ... si il n'y est pas -> pas dsp !!!
				if(cpt == 0) {return 0;}
				x->x_env_buf[cpt] = 0;

			}
		}
	}

	return 1;
}

#endif



// BUFFERS ENVELOPPE

void synGranul_setenv(t_synGranul *x, t_symbol *msg, short ac, t_atom * av)
{	
	// Changement du buffer enveloppe
#ifndef ARCH_PD
	t_buffer *b;
#else
	t_garray *a;
#endif
	int num;
	int tmp;
	t_symbol *s;
// 	post("synGranul_setenv %s",s->s_name);

	if(ac != 2)
	{
			post("synGranul~ : error setenv <#buffer> <buffer name> ");
			return;
	}
	
	num = (int) atom2float(av,0);
	if( num < 0 || num >= NEBUF )
	{
		post("synGranul~ : <#buffer> out of range");
		return;
	}
		
	if( !(s = atom2symbol(av,1)) )
		return;
	
	x->x_env_sym[num] = s;
	if(x->x_nenvbuffer < num + 1) x->x_nenvbuffer = num+1;
#ifndef ARCH_PD
	if ((b = (t_buffer *)(s->s_thing)) && ob_sym(b) == ps_buffer && b && b->b_valid)
	{
		x->x_env_buf[num] = b;
		x->x_env_sym[num] = s;
		x->x_env_samples[num] = b->b_samples;
		x->x_env_frames[num] = b->b_frames;
		synGranul_info(x,1);
	} else {
		error("synGranul~: no buffer~ %s", s->s_name);
		x->x_env_buf[num] = 0;
	}
#else
	if (!(a = (t_garray *)pd_findbyclass(x->x_env_sym[num], garray_class)))
	     {
        	 if (*s->s_name) pd_error(x, "syngranul~: %s: no such array",
        	     x->x_env_sym[num]->s_name);
        	 x->x_env_buf[num] = 0;
	     }
	     else if (!garray_getfloatarray(a, &x->x_env_frames[num], &x->x_env_samples[num]))
	     {
        	 pd_error(x, "%s: bad template for syngranul~", x->x_env_sym[num]->s_name);
        	 x->x_env_buf[num] = 0;
	     }
	     else garray_usedindsp(a);
#endif

}


// affectation du num�ro de buffer enveloppe actif
void synGranul_envbuffer(t_synGranul *x, t_plong n)
{
	x->x_active_env = (n < 0)? 0 : ((n >= NEBUF)? NEBUF-1 : n );
	
}


// DSP
#ifdef ARCH_PD
void synGranul_dsp(t_synGranul *x, t_signal **sp)
{
	int n = x->x_nouts ;
	x->x_sr = sys_getsr();

	
	x->x_in2con = x->x_in3con = x->x_in4con = x->x_in5con = x->x_in6con = x->x_in7con = x->x_in8con = 1;
	
	switch (n) 
	{	
		case 1 :
		dsp_add(synGranul_perform1, 10, x, 
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec,
		sp[7]->s_vec,
		sp[0]->s_n);
		break;
		
		case 2 :
		dsp_add(synGranul_perform2, 11, x, 
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec,
		sp[7]->s_vec, sp[8]->s_vec,
		sp[0]->s_n);
		break;
			
		case 4 :
		dsp_add(synGranul_perform4, 13, x, 
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, 
		sp[7]->s_vec, sp[8]->s_vec, sp[9]->s_vec, sp[10]->s_vec,
		sp[0]->s_n);
		break;
		
		case 6 :		
		dsp_add(synGranul_perform6, 15, x,  
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, 
		sp[7]->s_vec, sp[8]->s_vec,sp[9]->s_vec, sp[10]->s_vec,sp[11]->s_vec, sp[12]->s_vec,
		sp[0]->s_n);
		break ;
		
		case 8 :
		dsp_add(synGranul_perform8, 17, x,  
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, 
		sp[7]->s_vec, sp[8]->s_vec, sp[9]->s_vec, sp[10]->s_vec, sp[11]->s_vec, sp[12]->s_vec, sp[13]->s_vec, sp[14]->s_vec,
		sp[0]->s_n);
		break ;
	}
}
#else // MAX
void synGranul_dsp(t_synGranul *x, t_signal **sp, short *count)
{
	int n = x->x_nouts ;
	x->x_sr = sys_getsr();


	// signal connected to inlets ?
   	x->x_in2con = count[1] > 0;
	x->x_in3con = count[2] > 0;
	x->x_in4con = count[3] > 0; 
   	x->x_in5con = count[4] > 0;
	x->x_in6con = count[5] > 0;
	x->x_in7con = count[6] > 0;
	x->x_in8con = count[7] > 0;

	switch (n) 
	{	
		case 1 :
		dsp_add(synGranul_perform1, 10, x, 
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec,
		sp[7]->s_vec,
		sp[0]->s_n);
		break;
		
		case 2 :
		dsp_add(synGranul_perform2, 11, x, 
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec,
		sp[7]->s_vec, sp[8]->s_vec,
		sp[0]->s_n);
		break;
			
		case 4 :
		dsp_add(synGranul_perform4, 13, x, 
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, 
		sp[7]->s_vec, sp[8]->s_vec, sp[9]->s_vec, sp[10]->s_vec,
		sp[0]->s_n);
		break;
		
		case 6 :		
		dsp_add(synGranul_perform6, 15, x,  
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, 
		sp[7]->s_vec, sp[8]->s_vec,sp[9]->s_vec, sp[10]->s_vec,sp[11]->s_vec, sp[12]->s_vec,
		sp[0]->s_n);
		break ;
		
		case 8 :
		dsp_add(synGranul_perform8, 17, x,  
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, 
		sp[7]->s_vec, sp[8]->s_vec, sp[9]->s_vec, sp[10]->s_vec, sp[11]->s_vec, sp[12]->s_vec, sp[13]->s_vec, sp[14]->s_vec,
		sp[0]->s_n);
		break ;
	}
}
#endif

void synGranul_free(t_synGranul *x)
{
	if(x->x_linear_interp_table)
#ifndef ARCH_PD
		sysmem_freeptr(x->x_linear_interp_table);
#else
		freebytes(x->x_linear_interp_table,TABLE_SIZE * sizeof(struct linear_interp));
#endif
	synGranul_desalloc(x);
#ifndef ARCH_PD
	dsp_free((t_pxobject *) x);
#endif
}

// routines allocations

int synGranul_alloc(t_synGranul *x)
{
    
    x->x_ind 		= (long *) sysmem_newptr(NVOICES * sizeof(long));
    x->x_remain_ind = (long *) sysmem_newptr(NVOICES * sizeof(long));
    
    x->Vfreq		= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vphase		= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vamp			= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vlength		= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vpan			= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vdist		= (float *) sysmem_newptr(NVOICES * sizeof(float));
    
    x->Vphase_inc	= (float *) sysmem_newptr(NVOICES * sizeof(float));
    
    x->Vmem1		= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vmem2		= (float *) sysmem_newptr(NVOICES * sizeof(float));
    
    x->Vcoef		= (float *) sysmem_newptr(NVOICES * sizeof(float));
    
    x->envinc		= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->envind		= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->x_env		= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Venv			= (int *) sysmem_newptr(NVOICES * sizeof(int));
    
    x->x_delay		= (int *) sysmem_newptr(NVOICES * sizeof(int));
    x->x_voiceOn	= (int *) sysmem_newptr(NVOICES * sizeof(int));
    
    x->Vhp1			= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vhp2			= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vhp3			= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vhp4			= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vhp5			= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vhp6			= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vhp7			= (float *) sysmem_newptr(NVOICES * sizeof(float));
    x->Vhp8			= (float *) sysmem_newptr(NVOICES * sizeof(float));


    return !(!x->x_ind | !x->x_remain_ind | !x->Vfreq | !x->Vphase | !x->Vamp | !x->Vlength	| !x->Vpan | !x->Vdist | !x->Vphase_inc |
    			!x->Vmem1  | !x->Vmem2  | !x->Vcoef | !x->envinc | !x->envind | !x->x_env | !x->Venv | !x->x_delay | !x->x_voiceOn
    				 | !x->Vhp1 | !x->Vhp2 | !x->Vhp3 | !x->Vhp4 | !x->Vhp5 | !x->Vhp6 | !x->Vhp7 | !x->Vhp8 );
}

int synGranul_desalloc(t_synGranul *x)
{
#ifndef ARCH_PD
	if(x->x_ind)sysmem_freeptr(x->x_ind);
	if( x->x_remain_ind )sysmem_freeptr(x->x_remain_ind);
    
    if( x->Vfreq )sysmem_freeptr(x->Vfreq);
    if( x->Vphase )sysmem_freeptr(x->Vphase);
    if( x->Vamp )sysmem_freeptr(x->Vamp);
    if( x->Vlength )sysmem_freeptr( x->Vlength);
    if( x->Vpan )sysmem_freeptr(x->Vpan);
    if( x->Vdist )sysmem_freeptr( x->Vdist);
    
    if(  x->Vphase_inc)sysmem_freeptr(x->Vphase_inc);
    
    if( x->Vmem1 )sysmem_freeptr(x->Vmem1);
    if( x->Vmem2 )sysmem_freeptr(x->Vmem2);
    
    if( x->Vcoef )sysmem_freeptr( x->Vcoef);
    
    if( x->envinc )sysmem_freeptr(x->envinc);
    if(x->envind )sysmem_freeptr(x->envind);
    if( x->x_env )sysmem_freeptr(x->x_env);
    if(  x->Venv )sysmem_freeptr(x->Venv);
    
    if( x->x_delay )sysmem_freeptr(x->x_delay);
    if( x->x_voiceOn )sysmem_freeptr(x->x_voiceOn);
    
    if( x->Vhp1 )sysmem_freeptr(x->Vhp1);
    if( x->Vhp2 )sysmem_freeptr(x->Vhp2);
    if( x->Vhp3 )sysmem_freeptr(x->Vhp3);
    if( x->Vhp4 )sysmem_freeptr(x->Vhp4);
    if( x->Vhp5 )sysmem_freeptr(x->Vhp5);
    if( x->Vhp6 )sysmem_freeptr(x->Vhp6);
    if( x->Vhp7 )sysmem_freeptr(x->Vhp7);
	if( x->Vhp8 )sysmem_freeptr(x->Vhp8);
#else
	if(x->x_ind)freebytes(x->x_ind,  NVOICES * sizeof( long ));
	if( x->x_remain_ind )freebytes(x->x_remain_ind,  NVOICES * sizeof( long ));
    
    if( x->Vfreq )freebytes(x->Vfreq,  NVOICES * sizeof( float ));
    if( x->Vphase )freebytes(x->Vphase,  NVOICES * sizeof( float ));
    if( x->Vamp )freebytes(x->Vamp,  NVOICES * sizeof( float ));
    if( x->Vlength )freebytes( x->Vlength,  NVOICES * sizeof( float ));
    if( x->Vpan )freebytes(x->Vpan,  NVOICES * sizeof( float ));
    if( x->Vdist )freebytes( x->Vdist,  NVOICES * sizeof( float ));
    
    if(  x->Vphase_inc)freebytes(x->Vphase_inc,  NVOICES * sizeof( float ));
    
    if( x->Vmem1 )freebytes(x->Vmem1,  NVOICES * sizeof( float ));
    if( x->Vmem2 )freebytes(x->Vmem2,  NVOICES * sizeof( float ));
    
    if( x->Vcoef )freebytes( x->Vcoef,  NVOICES * sizeof( float ));
    
    if( x->envinc )freebytes(x->envinc,  NVOICES * sizeof( float ));
    if(x->envind )freebytes(x->envind,  NVOICES * sizeof( float ));
    if( x->x_env )freebytes(x->x_env,  NVOICES * sizeof( float ));
    if(  x->Venv )freebytes(x->Venv,  NVOICES * sizeof( int ));
    
    if( x->x_delay )freebytes(x->x_delay,  NVOICES * sizeof( int ));
    if( x->x_voiceOn )freebytes(x->x_voiceOn,  NVOICES * sizeof( int ));
    
    if( x->Vhp1 )freebytes(x->Vhp1,  NVOICES * sizeof( float ));
    if( x->Vhp2 )freebytes(x->Vhp2,  NVOICES * sizeof( float ));
    if( x->Vhp3 )freebytes(x->Vhp3,  NVOICES * sizeof( float ));
    if( x->Vhp4 )freebytes(x->Vhp4,  NVOICES * sizeof( float ));
    if( x->Vhp5 )freebytes(x->Vhp5,  NVOICES * sizeof( float ));
    if( x->Vhp6 )freebytes(x->Vhp6,  NVOICES * sizeof( float ));
    if( x->Vhp7 )freebytes(x->Vhp7,  NVOICES * sizeof( float ));
	if( x->Vhp8 )freebytes(x->Vhp8,  NVOICES * sizeof( float ));	
#endif
 return 1; 
}

//THE END, that's all folk!!!!!!
