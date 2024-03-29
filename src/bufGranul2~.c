/************************************************************************
 *
 *			                >>>>>>> BUFGRANUL2~ <<<<<<<
 *
 *						  multi-buffer enveloppe externe, 
 *                          multi-buffer son externe.
 *              continuite des grains lors d'un changement de buffer.
 *                         controle en float et/ou en audio
 * 					selection de buffer son par entree signal
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

#include "bufGranul2~.h"

void *bufGranul_class;
t_symbol *ps_buffer;


//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%//

void bufGranul_tilde_setup(void)
{ 
	bufGranul_class = class_new(gensym("bufGranul~"), (t_newmethod)bufGranul_new, (t_method)bufGranul_free, (short)sizeof(t_bufGranul), 0L, A_GIMME, 0);
        
	CLASS_MAINSIGNALIN(bufGranul_class, t_bufGranul, x_f);
	         
    class_addmethod(bufGranul_class, (t_method)bufGranul_set, gensym("set"), A_GIMME,0);		// definition du buffer~ son
    class_addmethod(bufGranul_class, (t_method)bufGranul_setenv, gensym("setenv"), A_GIMME, 0);	// definition du buffer~ enveloppe
	class_addmethod(bufGranul_class, (t_method)bufGranul_envbuffer, gensym("envbuffer"), A_FLOAT, 0);	// # env buffer actif     

    class_addmethod(bufGranul_class, (t_method)bufGranul_loop, gensym("loop"), A_GIMME, 0);		// infos du mode loop
	class_addmethod(bufGranul_class, (t_method)bufGranul_nvoices, gensym("nvoices"), A_FLOAT, 0);	// nombre de voix (polyphonie)       
 	class_addmethod(bufGranul_class, (t_method)bufGranul_tellme, gensym("tellme"),0);				// demande d'info
    class_addbang(bufGranul_class, (t_method)bufGranul_bang);    						// routine pour un bang, declenchement d'un grain
    //addfloat((t_method)bufGranul_float);							// affectation des valeurs par des flottants
   
	//class_addmethod((t_method)bufGranul_assist, gensym("assist"), A_CANT, 0);		// assistance in out 
    class_addmethod(bufGranul_class, (t_method)bufGranul_dsp, gensym("dsp"), 0);			// signal processing
    class_addmethod(bufGranul_class, (t_method)bufGranul_sinterp, gensym("sinterp"), A_FLOAT, 0);	// interpollation dans la lecture du buffer pour �viter les clics
    class_addmethod(bufGranul_class, (t_method)bufGranul_clear, gensym("clear"),0);				// panique ! effacement des grains en cours
    class_addmethod(bufGranul_class, (t_method)bufGranul_clear, gensym("panic"),0);				// panique ! effacement des grains en cours

	 class_addmethod(bufGranul_class, (t_method)bufGranul_grain, gensym("grain"), A_GIMME, 0);
	 
#ifdef PERF_DEBUG
    class_addmethod(bufGranul_class, (t_method)bufGranul_poll, gensym("poll"), A_FLOAT, 0);
    class_addmethod(bufGranul_class, (t_method)bufGranul_info, gensym("info"), A_FLOAT, 0);
#endif
    CLASS_MAINSIGNALIN(bufGranul_class, t_bufGranul, x_f);
    //dsp_initclass();
	//ps_buffer = gensym("buffer~");

	post("Copyright � 2012 BufGranul~ v2.0 Zorglub GMEM Marseille F") ;
	post("build %s %s",__DATE__,__TIME__);
}

// Reception d'un bang, declenche un grain
void bufGranul_bang (t_bufGranul *x)
{
	x->x_askfor = 1;
}

// declenchement grain par liste
// delay(ms) begin detune amp length pan dist buffer envbuffer
void bufGranul_grain(t_bufGranul *x, t_symbol *s, short ac, t_atom *av)
{
	int j,p;
	int nvoices = x->x_nvoices;
	int xn = 0;
	
	if(ac < 9)
	{	
		post("bufgranul~ : grain args are <delay(ms)> <begin> <detune> <amp> <length> <pan> <dist> <buffer> <envbuffer>");
		return;
	}

	
	double delay = atom2float(av,0);
	double begin = atom2float(av,1);
	double detune = atom2float(av,2);
	double amp = atom2float(av,3);
	double length = atom2float(av,4);
	double pan = atom2float(av,5);
	double dist = atom2float(av,6);
	
	int buffer = (int)atom2float(av,7);
	int envbuffer = (int)atom2float(av,8);
	
	double srms = x->x_sr*0.001; // double for precision
	
	begin = begin * srms;
	delay = delay * srms;
	p = nvoices;
	
	while(--p && x->x_voiceOn[p] ) { }  // avance jusqu a la 1ere voix libre
				
				if(p)
				{
    				x->x_voiceOn[p] = 1;
					x->x_sind[p] = x->Vbeg[p] = begin;	// index dans buffer
					x->Vtranspos[p] = detune ;			// valeur de pitch
					x->Vamp[p]		= amp;						// amplitude
					x->Vbuf[p] = buffer_check(x, buffer );		// numero du buffer son	
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
					pannerV(x,p);
					
					x->x_ind[p] = 0;
					x->x_remain_ind[p] = (long) x->Vlength[p];
					x->x_delay[p] = delay;   // delay de declenchement dans le vecteur         	  
    					
				}

	
}


// effacement de tous les grains en cours (panique)
void bufGranul_clear (t_bufGranul *x)
{
	int i;
	for (i=0; i< NVOICES; i++) x->x_voiceOn[i] = 0;
}

// voir en sortie le nombre de voix, le buffer son actif etc...
#ifdef PERF_DEBUG
	void bufGranul_poll(t_bufGranul *x, float n)
	{
		x->ask_poll = (n > 0 ? 1 : 0);
	}
	
#endif



float atom2float(t_atom * av, int ind)
{
	switch (av[ind].a_type)
		{
			case A_FLOAT :
				return av[ind].a_w.w_float;
				break;
			default :
				return 0.;
				
		}


}

t_symbol * atom2symbol(t_atom * av, int ind)
{
	switch (av[ind].a_type)
		{
			case A_SYMBOL :
				return av[ind].a_w.w_symbol;
				break;
			default :
				return 0;
				
		}


}

// Fonction de verification de la validit� du buffer demand�
int buffer_check(t_bufGranul *x, int num)
{
	int index;
	
	index = ( num < 0 || num >= NSBUF ) ? 0 : num ;
	
	
	return index;
}

// Fonction de verification de la validit� du buffer demand�
int bufferenv_check(t_bufGranul *x, int num)
{
	return ( x->x_env_buf[num] == 0 ) ? 0 : num ;
	
}


// Fonction pour spatialiser sur n voix
float spat (float x, float d, int n)
{
	float x1;
	
	x1 = fmod(x, 1.0);	
	x1 = (x1 < 0.5 ? 0.5 - x1 : x1 - 0.5) ;		// x1 = abs(x - 0.5) ;
	x1 = (1 - (x1 * n * d)) ;					// fct lineaire ;
	x1 = (x1 < 0 ? 0 : x1) ;					//  max(0, x1 );
	return pow(x1 , 0.5) * ((d / 2) + 0.5) ;	// (racine de x1) compensee pour la distance;
}

// Fonction pour spatialiser sur 2 voix
float spat2 (float x, float d)
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
void panner(t_bufGranul *x, float teta)
{  
	int n = x->x_nouts;
	float delta = 1./(float)n ;
	float d = x->x_dist;	
	
	teta = (teta < 0 ? 0 : teta) ;
	x->x_teta = teta;	
	
			
	switch (n)
	{
		case 1:
			break;
		case 2:
			x->x_hp1 = spat2( teta + 0.5, d) ;	
			x->x_hp2 = spat2( teta + 0.0, d) ;
		break;
		
		case 4:
			x->x_hp1 = spat( teta + 0.5, d, n) ;	
			x->x_hp2 = spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->x_hp3 = spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->x_hp4 = spat( teta + 0.5 + (1 * delta), d, n) ;
		break;
		
		case 6:
			x->x_hp1 = spat( teta + 0.5, d, n) ;	
			x->x_hp2 = spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->x_hp3 = spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->x_hp4 = spat( teta + 0.5 - (3 * delta), d, n) ;	
			x->x_hp5 = spat( teta + 0.5 + (2 * delta), d, n) ;	
			x->x_hp6 = spat( teta + 0.5 + (1 * delta), d, n) ;	
		break;
		
		case 8 :	
			x->x_hp1 = spat( teta + 0.5, d, n) ;	
			x->x_hp2 = spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->x_hp3 = spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->x_hp4 = spat( teta + 0.5 - (3 * delta), d, n) ;	
			x->x_hp5 = spat( teta + 0.5 - (4 * delta), d, n) ;	
			x->x_hp6 = spat( teta + 0.5 + (3 * delta), d, n) ;	
			x->x_hp7 = spat( teta + 0.5 + (2 * delta), d, n) ;	
			x->x_hp8 = spat( teta + 0.5 + (1 * delta), d, n) ;	
		break;
	}
}

// Panning suivant nombres de hps par voix(2-4-6-8)
void pannerV(t_bufGranul *x, int voice)
{  
	int n = x->x_nouts;
	float delta = 1./(float)n ;
	float d = ((x->Vdist[voice] < 0 ? 0 : x->Vdist[voice]) > 1 ? 1 : x->Vdist[voice]);
	float teta = (x->Vpan[voice] < 0 ? 0 : x->Vpan[voice]) ;
	switch (n)
	{
		case 2:
			x->Vhp1[voice] = spat2( teta + 0.5, d) ;	
			x->Vhp2[voice] = spat2( teta + 0.0, d) ;
		break;
		
		case 4:
			x->Vhp1[voice] = spat( teta + 0.5, d, n) ;	
			x->Vhp2[voice] = spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->Vhp3[voice] = spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->Vhp4[voice] = spat( teta + 0.5 + (1 * delta), d, n) ;
		break;
		
		case 6:
			x->Vhp1[voice] = spat( teta + 0.5, d, n) ;	
			x->Vhp2[voice] = spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->Vhp3[voice] = spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->Vhp4[voice] = spat( teta + 0.5 - (3 * delta), d, n) ;	
			x->Vhp5[voice] = spat( teta + 0.5 + (2 * delta), d, n) ;	
			x->Vhp6[voice] = spat( teta + 0.5 + (1 * delta), d, n) ;	
		break;
		
		case 8 :	
			x->Vhp1[voice] = spat( teta + 0.5, d, n) ;	
			x->Vhp2[voice] = spat( teta + 0.5 - (1 * delta), d, n) ;	
			x->Vhp3[voice] = spat( teta + 0.5 - (2 * delta), d, n) ;	
			x->Vhp4[voice] = spat( teta + 0.5 - (3 * delta), d, n) ;	
			x->Vhp5[voice] = spat( teta + 0.5 - (4 * delta), d, n) ;	
			x->Vhp6[voice] = spat( teta + 0.5 + (3 * delta), d, n) ;	
			x->Vhp7[voice] = spat( teta + 0.5 + (2 * delta), d, n) ;	
			x->Vhp8[voice] = spat( teta + 0.5 + (1 * delta), d, n) ;	
		break;
	}
}

// Reception des valeurs sur les entrees non-signal
/*void bufGranul_float(t_bufGranul *x, double f)
{
	if (x->x_obj.z_in == 0)
		post("a float in first inlet don't do anything");
	else if (x->x_obj.z_in == 1)
		x->x_begin = (f>0.) ? f : 0. ;
	else if (x->x_obj.z_in == 2)
		x->x_transpos = f;
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
									panner(x, x->x_pan);
									// necessaire si on ne fait pas le calcul
									// dans la perform quand signal non connecte
								}		
	else if (x->x_obj.z_in == 6){	 
									x->x_dist = f;
									panner(x, x->x_pan);
									// necessaire si on ne fait pas le calcul
									// dans la perform quand signal non connecte
								}
	else if (x->x_obj.z_in == 7){	 
									x->x_active_buf = buffer_check(x,(int)f);
									// necessaire si on ne fait pas le calcul
									// dans la perform quand signal non connecte
								}		
}*/

// limitation de la polyphonie
void bufGranul_nvoices(t_bufGranul *x, float n) 
{ 
	n = (n > 0) ? n : 1 ;
	x->x_nvoices = (n < NVOICES) ? n+1 : NVOICES; 
}

// interpollation dans la lecture du buffer (evite clic, crack et autre pop plop)
void bufGranul_sinterp(t_bufGranul *x, float n)
{ 
	x->x_sinterp = n ? 1 : 0 ; 
}

// mode loop begin end ....
void bufGranul_loop(t_bufGranul *x, t_symbol *s, short ac, t_atom *av)
{
	int j;
	
	if(ac < 1)
	{	
		post("bufgranul~ : loop bad args... loop <mode> [<begin> <end>]");
		return;
	}
	if(ac == 1 && av[0].a_type == A_FLOAT)
	{
		x->x_loop = (av[0].a_w.w_float != 0);
		return;
	}
	if(ac == 3 && av[0].a_type == A_FLOAT)
	{
		if(av[0].a_w.w_float != 0)
		{
			// loopstart
				switch (av[1].a_type){
				
				case A_FLOAT:
				x->x_loopstart = MAX(av[1].a_w.w_float,0);
				break;
				
				default : 
					post("bugranul~ : loop bad arg");
					x->x_loop = 0 ;
					return;
				}
			// loopend
				switch (av[2].a_type){
    	
				
				case A_FLOAT:
				x->x_loopend = MAX(av[2].a_w.w_float,x->x_loopstart + MIN_LOOP_LENGTH);
				break;
				
				default :
					post("bugranul~ : loop bad arg");
					x->x_loop = 0 ;
					return;
				}
				
			x->x_loop = 2;
			//post("start %f end %f",x->x_loopstart,x->x_loopend);
		}
		else
			x->x_loop = 0 ;
	}else
	{
		x->x_loop = 0 ;
		post("bufgranul~ : loop bad args... loop <mode> [<begin> <end>]");
	}
	
}


// Impression de l'etat de l'objet
void bufGranul_tellme(t_bufGranul *x)
{
	int i;
	post("  ");
	post("_______BufGranul~'s State_______"); 

	post("::global settings::");

	post("outputs : %ld",x->x_nouts);
    post("voices : %ld",x->x_nvoices);
	post("________________________________");

	post("::sound buffers::");

    post("-- Active sound buffer : %ld --",x->x_active_buf+1);
	post("-- Number of sound buffers : %ld --",x->x_nbuffer);
	for (i=0; i< x->x_nbuffer ; i++)
	{
		if(x->x_buf_sym[i]->s_name) post("   sound buffer~ %ld name : %s",i+1,x->x_buf_sym[i]->s_name);
		if(x->x_buf_filename[i]->s_name) post("   sound buffer~ %ld filename : %s",i+1,x->x_buf_filename[i]->s_name);
	}
	post("________________________________");

	post("::envelope buffers::");

    post("-- Active envelope buffer : %ld --",x->x_active_env+1);
	post("-- Number of envelope buffers : %ld --",x->x_nenvbuffer);
	for (i=0; i< x->x_nenvbuffer ; i++)
	{
		if(x->x_env_sym[i]->s_name) post("   envelope buffer~ %ld name : %s",i+1,x->x_env_sym[i]->s_name);
	}
	post("________________________________");
	
	bufGranul_info(x,-1);


}

// Informations en sortie de l'objet
#ifdef PERF_DEBUG
	void   bufGranul_info(t_bufGranul *x, float n)
	{
		// Si n=	0 infos sur buffer son
		//			1 infos sur buffer enveloppe
		//		   -1 infos sur buffers...
		int i;
		
		if(x->ask_poll)
		{
			if(n==0 || n==-1)
			{
				for(i=0; i<x->x_nbuffer; i++)
				{
					SETSYMBOL( x->info_list, gensym("buffer"));
					//if(x->x_buf[i])
					//{
						SETFLOAT(x->info_list+1, i);
						
						if(x->x_buf_sym[i] && x->x_buf_sym[i]->s_name)
							SETSYMBOL(x->info_list+2, x->x_buf_sym[i]);
						else
							SETSYMBOL(x->info_list+2,gensym("unknown name"));
							
						if(x->x_buf_filename[i] && x->x_buf_filename[i]->s_name)
							SETSYMBOL(x->info_list+3, x->x_buf_filename[i]);
						else
							SETSYMBOL(x->info_list+3,gensym("unknown filename"));
						
						if(x->x_buf_frames[i])
							SETFLOAT(x->info_list+4,  x->x_buf_frames[i]/44.1);
						else
							SETFLOAT(x->info_list+4,0.);
					
							outlet_list(x->info,&s_list,5,x->info_list);
						}
					//}
				}
			
			if(n==1 || n==-1)
			{
				SETSYMBOL( x->info_list, gensym("active_env"));
				SETFLOAT(x->info_list+1,  x->x_active_env+1 ); 
				outlet_list(x->info,&s_list,2,x->info_list);
				
				for(i=0; i<x->x_nenvbuffer; i++)
				{
					if(x->x_env_sym[i])
					{
						SETSYMBOL(x->info_list, gensym("envbuffer"));
						SETFLOAT(x->info_list+1, i);
						SETSYMBOL(x->info_list+2, x->x_env_sym[i]); 
						outlet_list(x->info,&s_list,3,x->info_list);
					}
				}
			}
		}
}
	

#endif


//NNNNNNN routine de creation de l'objet NNNNNNNNNNNNNNNNNNNNNNNNNNNN//

void *bufGranul_new(t_symbol *s, short ac, t_atom *av)
{   
		
	int i,j,symcount = 0, k; 
	float f;
    t_bufGranul *x = (t_bufGranul *)pd_new(bufGranul_class); 
    //dsp_setup((t_pxobject *)x,1);  
          
    //creation des entres supplementaires---//

	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
	    
    x->x_nouts = 2;	// pour que nb outs = 2 si pas specifie
    x->x_f = 0;
    
	if(ac < 2)
	{
		post("Missing Arguments 1 Sound Buffer Name 2 Envelope Buffer Name 3 Outputs number (1-2-4-6-8)");
		return 0;
	} else {

		//////////////arguments/////////////////
		// note : 1er symbol buffer son , 2eme symbol buffer env, 3eme nombres de sorties

		for (i=0; i< NSBUF; i++)
		{
			x->x_buf[i]=0;
			x->x_buf_filename[i]=gensym("empty_buffer");
    		x->x_buf_sym[i]=gensym("empty_buffer");
			x->x_buf_nchan[i]=-1;
			x->x_buf_samples[i]=0;
			x->x_buf_frames[i]=-1;
		}

		for (i=0; i < NEBUF; i++)
		{
			x->x_env_buf[i]=0;
    		x->x_env_sym[i]=gensym("empty_buffer");
			x->x_env_samples[i]=0;
			x->x_env_frames[i]=-1;
		}

		for (j=0; j < ac; j++){
    		switch (av[j].a_type){
    		
    			case A_FLOAT:
    			//	post("argument %ld is a float : %f, assigned to nb outs", (long)j,av[j].a_w.w_float);
    				x->x_nouts = av[j].a_w.w_float;
    			break;
    			
    			case A_SYMBOL:
    			if(symcount)
    			{
    			//	post("argument %ld is a symbol : name %s, assigned to env buffer~ name",(long)j,av[j].a_w.w_symbol->s_name);

    					x->x_env_sym[0] = av[j].a_w.w_symbol;

    			}
    			else
				{
    			//	post("argument %ld is a symbol : name %s, assigned to buffer~ name",(long)j,av[j].a_w.w_symbol->s_name);

    					x->x_buf_sym[0] = av[j].a_w.w_symbol;
    				symcount++;
    			}
    			break;
    			
    		}	
		}

		///////////////////////////////
		 // info outlet
		
		
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
		x->info = outlet_new(&x->x_obj, &s_list);
		#endif
		
		//allocations des tableaux
		if( !bufGranul_alloc(x))
		{
			post("error bufGranul~ : not enough memory to instanciate");
		}

		// initialisation des autres parametres
		x->x_nvoices = 64;
		x->x_askfor = 0;    
		x->x_begin = 0;
		x->x_transpos = 1.0;
		x->x_amp = 1.0;    
		x->x_length = 100;
    
		x->x_sinterp = 1;
		 x->x_loop = 0 ;
    
		x->x_pan = 1/(x->x_nouts*2);
		x->x_dist = 1;
		panner(x, x->x_pan); 
		x->x_nvoices_active = 0;  
		x->x_env_dir = 1;
        
		for (i=0; i< NVOICES; i++){       
     		x->x_ind[i] = -1;
     		x->x_env[i] = 0;
     		x->x_voiceOn[i] = 0;
			x->Vbuf[i] = 0;
		}

		// initialisation des parametres d'activite (modifie par la suite dans setenv et set)
		x->x_nbuffer = 1;
		x->x_active_buf = 0;
		x->x_nenvbuffer = 1;
		x->x_active_env = 0;
		
		for( i=1 ; i<NSBUF ; i++)
		{
			x->x_buf[i] = 0 ;
			x->x_buf_sym[i] = 0;
			x->x_env_buf[i] = 0;
			x->x_env_sym[i] = 0;
			x->x_buf_valid_index[i] = 0;
		}

		// generation de la table de coeff d'interpolation
		x->x_linear_interp_table = (t_linear_interp *) getbytes( TABLE_SIZE * sizeof(struct linear_interp) );
		
		if( ! x->x_linear_interp_table)
		{
			post("bufGranul~ : not enough memory to instanciate");
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

//--------assitance information inlet et out1et----------------------//
void bufGranul_assist(t_bufGranul *x, void *b, long m, long a, char *s)
{
	switch(m) {
		case 1: // inlet
			switch(a) {			
				case 0:
				sprintf(s, " bang,set, signal zero X-ing, et cetera)");		break;
				case 1:
				sprintf(s, " begin (float & signal) ");						break;				
				case 2:
				sprintf(s, " transpos (float & signal) ");					break;
				case 3:
				sprintf(s, " amp (float & signal) ");						break;
				case 4:
				sprintf(s, " length (float & signal) ");					break;
				case 5:
				sprintf(s, " pan (float & signal) ");						break;																				
				case 6:
				sprintf(s, " dist (float & signal) ");						break;
				case 7:
				sprintf(s, " sound buffer num (float & signal) ");			break;																				
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

//-----------------------------------------------------------------------------------//



// BUFFERS SON
// fonction d'assignation du buffer son #n
void bufGranul_set(t_bufGranul *x, t_symbol *msg, short ac, t_atom * av)
{
	// Changement du buffer son
	t_garray *a;
	int tmp;
	int num;
	t_symbol *s;
// 	post("bufGranul_set %s",s->s_name);
	
	if(ac == 2)
	{
		num = (int) atom2float(av,0);
		if( num < 0 || num >= NSBUF )
		{
			post("bufGranul~ : <#buffer> out of range");
			return;
		}
			
		if( !(s = atom2symbol(av,1)) )
			return;
	
	}else if(ac == 1)
	{
		if( !(s = atom2symbol(av,0)) )
		return;
		num = 0;
	}else
	{
		post("bufGranul~ : error set <#buffer> <buffer name> ");
			return;
	}
	
	x->x_buf_sym[num] = s;
	if(x->x_nbuffer < num + 1) x->x_nbuffer = num+1;
	
	/*if ( (b = (t_buffer *)(s->s_thing)) && ob_sym(b) == ps_buffer && b && b->b_valid)
	{

		x->x_buf_filename[num] = b->b_filename;
		x->x_buf_nchan[num] = b->b_nchans;
		x->x_buf_samples[num] = b->b_samples;
		x->x_buf_frames[num] = b->b_frames;//
		x->x_buf[num] = b;
		// On change de buffer actif
		//x->x_active_buf = tmp;

		

		bufGranul_info(x,0);

	} else {
		error("bufGranul~: no buffer~ %s", s->s_name);
		x->x_buf[num] = 0;
	}*/
	
	if (!(a = (t_garray *)pd_findbyclass(x->x_buf_sym[num], garray_class)))
	     {
        	 if (*s->s_name) pd_error(x, "bufgranul~: %s: no such array",
        	     x->x_buf_sym[num]->s_name);
        	 x->x_buf[num] = 0;
	     }
	     else if (!garray_getfloatarray(a, &x->x_buf_frames[num], &x->x_buf_samples[num]))
	     {
        	 pd_error(x, "%s: bad template for bufgranul~", x->x_buf_sym[num]->s_name);
        	 x->x_buf[num] = 0;
	     }
	     else garray_usedindsp(a);

}

int bufGranul_bufferinfos(t_bufGranul *x)
{
	int cpt;
	t_garray *a;
	t_symbol *s;
	long loopstart, loopend, looplength;
	float srms = x->x_sr*0.001;
	int loop = x->x_loop;
	
	// retourne 0 : sortie nulle
	// retourne 1 : calcul des grains

	// Test si enabled
    //if (x->x_obj.z_disabled) return -1;
    
    // init des index � 0
    memset(x->x_buf_valid_index,0,NSBUF*sizeof(int));

	// Test sur la validit� et l'activite des buffers & initialisations
    for(cpt = 0; cpt < x->x_nbuffer ; cpt++)
	{
		if( x->x_buf_sym[cpt] ) // si un nom de buffer a �t� donn� -> collecter les infos
		{
			if (!(a = (t_garray *)pd_findbyclass(x->x_buf_sym[cpt], garray_class)))
			     {
        			 if (x->x_buf_sym[cpt]->s_name) pd_error(x, "bufgranul~: %s: no such array",
        			     x->x_buf_sym[cpt]->s_name);
        			 x->x_buf[cpt] = 0;
				 if(cpt == 0) {return 0;}
			     }
			     else if (!garray_getfloatarray(a, &x->x_buf_frames[cpt], &x->x_buf_samples[cpt]))
			     {
        			 pd_error(x, "%s: bad template for bufgranul~", x->x_buf_sym[cpt]->s_name);
        			 x->x_buf[cpt] = 0;
				 if(cpt == 0) {return 0;}
			     }
			     else 
			     {
			     	garray_usedindsp(a);
				x->x_buf[cpt] = a;
			     	x->x_buf_valid_index[cpt] = cpt;
				x->x_buf_nchan[cpt] = 1;
				x->x_buf_sronsr[cpt] = 1.;
				
				if(loop)
					{    
						if(loop == 1){   
							loopstart = 0;      
							loopend = x->x_buf_frames[cpt];
							looplength = loopend - loopstart;}
						if(loop == 2){
							loopstart = SAT(x->x_loopstart*srms,0,x->x_buf_frames[cpt]);
							loopend = SAT(x->x_loopend*srms,0,x->x_buf_frames[cpt]);
							looplength = loopend - loopstart;}
							
						x->x_buf_loopstart[cpt] = loopstart;
						x->x_buf_loopend[cpt] = loopend;
						x->x_buf_looplength[cpt] = looplength;
					}
			     }
			
		}
	}

	for(cpt = 0; cpt < x->x_nenvbuffer ; cpt++)
	{
		if( x->x_env_sym[cpt] ) // si un nom de buffer a �t� donn� -> collecter les infos
		{
		
			if (!(a = (t_garray *)pd_findbyclass(x->x_env_sym[cpt], garray_class)))
			     {
        			 if (x->x_env_sym[cpt]->s_name) pd_error(x, "bufgranul~: %s: no such array",
        			     x->x_env_sym[cpt]->s_name);
        			 x->x_env_buf[cpt] = 0;
				 if(cpt == 0) {return 0;}
			     }
			     else if (!garray_getfloatarray(a, &x->x_env_frames[cpt], &x->x_env_samples[cpt]))
			     {
        			 pd_error(x, "%s: bad template for bufgranul~", x->x_buf_sym[cpt]->s_name);
        			 x->x_env_buf[cpt] = 0;
				 if(cpt == 0) {return 0;}
			     }
			     else 
			     {
					garray_usedindsp(a);
					x->x_env_buf[cpt] = cpt;
				}
			
		}
	}

	return 1;
}



// BUFFERS ENVELOPPE

void bufGranul_setenv(t_bufGranul *x, t_symbol *msg, short ac, t_atom * av)
{	
	// Changement du buffer enveloppe
	t_garray *a;
	int num;
	int tmp;
	t_symbol *s;
// 	post("bufGranul_setenv %s",s->s_name);

	if(ac == 2)
	{
		num = (int) atom2float(av,0);
		if( num < 0 || num >= NEBUF )
		{
			post("bufGranul~ : <#buffer> out of range");
			return;
		}
			
		if( !(s = atom2symbol(av,1)) )
			return;
		
	}else if(ac == 1)
	{
		if( !(s = atom2symbol(av,0)) )
		return;
		num = 0;
		
	}else
	{	
		post("bufGranul~ : error setenv <#buffer> <buffer name> ");
			return;
	}
	
	x->x_env_sym[num] = s;
	if(x->x_nenvbuffer < num + 1) x->x_nenvbuffer = num+1;

	/*if ((b = (t_buffer *)(s->s_thing)) && ob_sym(b) == ps_buffer && b && b->b_valid)
	{
		x->x_env_buf[num] = b;
		x->x_env_sym[num] = s;
		x->x_env_samples[num] = b->b_samples;
		x->x_env_frames[num] = b->b_frames;
		bufGranul_info(x,1);
	} else {
		error("bufGranul~: no buffer~ %s", s->s_name);
		x->x_env_buf[num] = 0;
	}*/
	
	if (!(a = (t_garray *)pd_findbyclass(x->x_env_sym[num], garray_class)))
	     {
        	 if (*s->s_name) pd_error(x, "bufgranul~: %s: no such array",
        	     x->x_env_sym[num]->s_name);
        	 x->x_env_buf[num] = 0;
	     }
	     else if (!garray_getfloatarray(a, &x->x_env_frames[num], &x->x_env_samples[num]))
	     {
        	 pd_error(x, "%s: bad template for bufgranul~", x->x_env_sym[num]->s_name);
        	 x->x_env_buf[num] = 0;
	     }
	     else garray_usedindsp(a);

}


// affectation du num�ro de buffer enveloppe actif
void bufGranul_envbuffer(t_bufGranul *x, float n)
{
	x->x_active_env = (n < 0)? 0 : ((n >= NEBUF)? NEBUF-1 : n );
	
}


// DSP

void bufGranul_dsp(t_bufGranul *x, t_signal **sp)
{
	int n = x->x_nouts ;
	x->x_sr = sys_getsr();


	// signal connected to inlets ?
   	//x->x_in2con = count[1] > 0;
	//x->x_in3con = count[2] > 0;
	//x->x_in4con = count[3] > 0; 
   	//x->x_in5con = count[4] > 0;
	//x->x_in6con = count[5] > 0;
	//x->x_in7con = count[6] > 0;
	//x->x_in8con = count[7] > 0;
	
	// TODO : clean this ���
	x->x_in2con = x->x_in3con = x->x_in4con = x->x_in5con = x->x_in6con = x->x_in7con = x->x_in8con = 1;
	
	
	//post("sig connected : %d %d %d %d %d",x->x_in2con,x->x_in3con,x->x_in4con,x->x_in5con,x->x_in6con);
	switch (n) 
	{	
		case 1 :
		dsp_add(bufGranul_perform1, 11, x, 
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, sp[7]->s_vec,
		sp[8]->s_vec,
		sp[0]->s_n);
		break;
		
		case 2 :
		dsp_add(bufGranul_perform2, 12, x, 
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, sp[7]->s_vec,
		sp[8]->s_vec, sp[9]->s_vec,
		sp[0]->s_n);
		break;
			
		case 4 :
		dsp_add(bufGranul_perform4, 14, x, 
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, sp[7]->s_vec, 
		sp[8]->s_vec, sp[9]->s_vec, sp[10]->s_vec, sp[11]->s_vec,
		sp[0]->s_n);
		break;
		
		case 6 :		
		dsp_add(bufGranul_perform6, 16, x,  
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, sp[7]->s_vec, 
		sp[8]->s_vec,sp[9]->s_vec, sp[10]->s_vec,sp[11]->s_vec, sp[12]->s_vec, sp[13]->s_vec,
		sp[0]->s_n);
		break ;
		
		case 8 :
		dsp_add(bufGranul_perform8, 18, x,  
		sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, sp[7]->s_vec,
		sp[8]->s_vec, sp[9]->s_vec, sp[10]->s_vec, sp[11]->s_vec, sp[12]->s_vec, sp[13]->s_vec, sp[14]->s_vec, sp[15]->s_vec,
		sp[0]->s_n);
		break ;
	}
}

void bufGranul_free(t_bufGranul *x)
{
	
	bufGranul_desalloc(x);
	
	if(x->x_linear_interp_table)
		freebytes(x->x_linear_interp_table,TABLE_SIZE * sizeof(struct linear_interp));

	//dsp_free((t_pxobject *) x);
}

// routines allocations

int bufGranul_alloc(t_bufGranul *x)
{
    
    if( !(x->x_ind 		= (long *) getbytes(NVOICES * sizeof(long)))) return 0; 
    if( !(x->x_remain_ind = (long *) getbytes(NVOICES * sizeof(long)))) return 0;
    if( !(x->x_sind 		= (double *) getbytes(NVOICES * sizeof(double)))) return 0;
    
    if( !(x->Vbeg		= (double *) getbytes(NVOICES * sizeof(double)))) return 0;
    if( !(x->Vtranspos	= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vamp		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vlength		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vpan		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vdist		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;

   	if( !(x->Vbuf		= (int *) getbytes(NVOICES * sizeof(int)))) return 0;

    if( !(x->envinc		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->envind		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->x_env		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Venv		= (int *) getbytes(NVOICES * sizeof(int)))) return 0;

    if( !(x->x_delay		= (int *) getbytes(NVOICES * sizeof(int)))) return 0;
    if( !(x->x_voiceOn	= (int *) getbytes(NVOICES * sizeof(int)))) return 0;
    
    if( !(x->Vhp1		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vhp2		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vhp3		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vhp4		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vhp5		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vhp6		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vhp7		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;
    if( !(x->Vhp8		= (float *) getbytes(NVOICES * sizeof(float)))) return 0;

    return !(!x->x_ind | !x->x_remain_ind | !x->x_sind | !x->Vbeg | !x->Vtranspos | !x->Vamp | !x->Vlength	| !x->Vpan | !x->Vdist |
    			!x->Vbuf| !x->envinc | !x->envind | !x->x_env | !x->Venv | !x->x_delay | !x->x_voiceOn
    				 | !x->Vhp1 | !x->Vhp2 | !x->Vhp3 | !x->Vhp4 | !x->Vhp5 | !x->Vhp6 | !x->Vhp7 | !x->Vhp8 );
}

int bufGranul_desalloc(t_bufGranul *x)
{
	
 	freebytes(x->x_ind, NVOICES * sizeof(long));
	freebytes(x->x_remain_ind, NVOICES * sizeof(long));
	freebytes(x->x_sind, NVOICES * sizeof(double));
    
    freebytes(x->Vbeg, NVOICES * sizeof(double));
    freebytes(x->Vtranspos, NVOICES * sizeof(float));
    freebytes(x->Vamp, NVOICES * sizeof(float));
    freebytes( x->Vlength, NVOICES * sizeof(float));
    freebytes(x->Vpan, NVOICES * sizeof(float));
    freebytes( x->Vdist, NVOICES * sizeof(float));
    
    freebytes(x->Vbuf, NVOICES * sizeof(int));
    
    freebytes(x->envinc, NVOICES * sizeof(float));
    freebytes(x->envind, NVOICES * sizeof(float));
    freebytes(x->x_env, NVOICES * sizeof(float));
    freebytes(x->Venv, NVOICES * sizeof(int));
    
    freebytes(x->x_delay, NVOICES * sizeof(int));
    freebytes(x->x_voiceOn, NVOICES * sizeof(int));
    
    freebytes(x->Vhp1, NVOICES * sizeof(float));
    freebytes(x->Vhp2, NVOICES * sizeof(float));
    freebytes(x->Vhp3, NVOICES * sizeof(float));
    freebytes(x->Vhp4, NVOICES * sizeof(float));
    freebytes(x->Vhp5, NVOICES * sizeof(float));
    freebytes(x->Vhp6, NVOICES * sizeof(float));
    freebytes(x->Vhp7, NVOICES * sizeof(float));
    freebytes(x->Vhp8, NVOICES * sizeof(float));
	
 return 1; 
}

//THE END, that's all hulk!!!!!!
