/* Born variable-density variable-velocity acoustic 2D time-domain FD modeling */
/*
The code uses a standard second-order stencil in time.
The coefficients of the spatial stencil are computed 
by matching the transfer function of the discretized 
first-derivative operator to the ideal response. 
The optimized coefficients minimize dispersion 
given that particular size of the stencil.

The term 
	ro div (1/ro grad (u))
where
	ro   : density
	div  : divergence op
	grad : gradient  op
	u    : wavefield
	
is implemented in order to obtain a positive semi-definite operator.

The "reflectivity" that is used in the code is intended to be function of the 
change in VELOCITY. In particular, it is supposed to be equal to the product between 
the background and the perturbation in the velocity field, that is, the linear term in
the perturbation when you expand the square of the perturbed velocity
	
	v^2 = (vo + vp)^2 ~ vo^2 + 2*vo*vp
	
by assuming the perturbation is small compared to the background, the term vp^2 
can be neglected. The factor 2 is included in the source injection term in the code.

============= FILE DESCRIPTIONS   ========================      

Fdat.rsf - An RSF file containing your data in the following format:
			axis 1 - Receiver location
			axis 2 - Time
			
Fwav.rsf - An RSF file containing your VOLUME DENSITY INJECTION RATE 
           wavelet information.  The sampling interval, origin time, 
           and number of time samples will be used as the defaults for the modeling code.
	       i.e. your wavelet needs to have the same length and parameters that you want to model with!
	       The first axis is the number of source locations.
	       The second axis is time.
		   
Fvel.rsf - An N dimensional RSF file that contains the values for the velocity field at every point in the computational domain.
		
Fden.rsf - An N dimensional RSF file that contains the values for density at every point in the computational domain.

Fref.rsf - Reflectivity (same dimensions of the velocity model)

Frec.rsf - Coordinates of the receivers
		axis 1 - (x,z) of the receiver
		axis 2 - receiver index

Fsou.rsf - Coordinate of the sources
		axis 1 - (x,y,z) of the source
		axis 2 - source index

Fwfl.rsf - Output wavefield

Fliw.rsf - linearized scattered wavefield

Flid.rsf - linearized scattered data

verb=y/n - verbose flag

snap=y/n - wavefield snapshots flag

free=y/n - free surface on/off



*/
/*
  Copyright (C) 2013 Colorado School of Mines
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <rsf.h>
#include "prep_utils.h"
#include "kernels.h"
#include<time.h>

int main(int argc, char* argv[])
{
  // command line parameters
  in_para_struct_t in_para;
  // running mode parameters
  born_setup_struct_t born_para;

  /* I/O files */
  // FWD and ADJ
  sf_file Fwav=NULL; /* wavelet   */
  sf_file Fsou=NULL; /* sources   */
  sf_file Frec=NULL; /* receivers */
  sf_file Fvel=NULL; /* velocity  */
  sf_file Fden=NULL; /* density   */

  // input for FWD, output for ADJ
  sf_file Fvpert=NULL; /* velocity perturbation */
  sf_file Frpert=NULL; /* density perturbation */

  // output FWD
  sf_file Fbdat=NULL; /* background data */
  // input ADJ
  sf_file Fsdat=NULL; /* scattered data */

  // auxiliary
  sf_file Fbwfl=NULL; /* background wavefield */
  sf_file Fswfl=NULL; /* scattered wavefield */

  /* Other parameters */
  bool verb;
  bool fsrf;
  bool adj;
  bool dabc;
  int nb;

  /* for benchmarking */
  clock_t start_bck_t, end_bck_t;
  float total_bck_t=0;
  clock_t start_fwd_t, end_fwd_t;
  float total_fwd_t=0;
  clock_t start_adj_t, end_adj_t;
  float total_adj_t=0;
  clock_t start_vsrc_t,end_vsrc_t;
  clock_t start_psrc_t,end_psrc_t;
  clock_t start_bextrap_t,end_bextrap_t;
  clock_t start_vstk_t,end_vstk_t;
  clock_t start_pstk_t,end_pstk_t;
  float total_vsrc_t=0;
  float total_psrc_t=0;
  float total_bextrap_t=0;
  float total_vstk_t=0;
  float total_pstk_t=0;

  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  /*                   RSF INITIALISATION                       */
  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  sf_init(argc,argv);
  init_param(&in_para);
  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  /*                   PARSE THE PARAMETERS                     */
  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  if (!sf_getbool("verb",&verb)) verb=true;   /* verbosity flag*/
  if (!sf_getbool("free",&fsrf)) fsrf=false;  /* free surface */
  if (!sf_getbool("adj",&adj))    adj=false;  /* adjoint flag */
  if(! sf_getbool("dabc",&dabc)) dabc=false; /* Absorbing BC */

  if( !sf_getint("nb",&nb)) nb=NOP; /* thickness of the absorbing boundary: NOP is the width of the FD stencil*/
  if (nb<NOP) nb=NOP;

  // fill the structure;
  in_para.verb=verb;
  in_para.fsrf=fsrf;
  in_para.dabc=dabc;
  in_para.adj=adj?ADJ:FWD;
  in_para.snap=true;  // we need the source wavefield
  in_para.nb=nb;

  if (in_para.verb)
    print_param(in_para);

  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  /*                       OPEN FILES                           */
  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  Fwav = sf_input ("in" );  /* wavelet   */
  Fvel = sf_input ("vel");  /* velocity  */
  Fden = sf_input ("den");  /* density   */
  Fsou = sf_input ("sou");  /* sources   */
  Frec = sf_input ("rec");  /* receivers */

  switch(in_para.adj){
  case FWD:
    // FWD BORN OPERATOR
    born_para.inputVelPerturbation=true;
    Fvpert= sf_input ("vpert");  /* velocity perturbation */

    born_para.inputDenPerturbation=false;
    if (sf_getstring("rpert")){
      Frpert= sf_input ("rpert");  /* density perturbation */
      born_para.inputDenPerturbation=true;
    }

    // these are aux output of the born forward modeling
    born_para.outputBackgroundWfl=false;
    if (sf_getstring("bwfl")){
      Fbwfl = sf_output("bwfl");  /* background wavefield*/
      born_para.outputBackgroundWfl=true;
    }
    else{
      born_para.Fbwfl = sf_tempfile(&(born_para.bckwflfilename),"w+");
    }

    born_para.outputBackgroundData=false;
    if (sf_getstring("bdat")){
      Fbdat = sf_output("bdat");  /* background data*/
      born_para.outputBackgroundData=true;
    }

    born_para.outputScatteredWfl=false;
    if (sf_getstring("swfl")){
      Fswfl = sf_output("swfl");  /* scattered wavefield*/
      born_para.outputScatteredWfl=true;
    }

    // this is the output of the born forward modeling
    Fsdat = sf_output("out");  /* scattered data*/
    break;
  case ADJ:
    // ADJ BORN OPERATOR

    Fsdat = sf_input("pdata"); /* input pressure data to backproject (REQUIRED)*/

    born_para.outputVelPertImage=true;
    Fvpert= sf_output ("out");  /* default output: velocity perturbation image */

    born_para.outputDenPertImage=false;
    if (sf_getstring("rpert")){
      Frpert= sf_output ("rpert");  /* density perturbation */
      born_para.outputDenPertImage=true;
      born_para.Fpv1 = sf_tempfile(&(born_para.pv1wflfilename),"w+");
      born_para.Fpv2 = sf_tempfile(&(born_para.pv2wflfilename),"w+");
    }

    // these are aux output of the born forward modeling
    born_para.outputBackgroundWfl=false;
    if (sf_getstring("bwfl")){
      Fbwfl = sf_output("bwfl");  /* background wavefield*/
      born_para.outputBackgroundWfl=true;
    }
    else{
      born_para.Fbwfl = sf_tempfile(&(born_para.bckwflfilename),"w+");
    }

    born_para.outputBackgroundData=false;
    if (sf_getstring("bdat")){
      Fbdat = sf_output("bdat");  /* background data*/
      born_para.outputBackgroundData=true;
    }

    born_para.outputScatteredWfl=false;
    if (sf_getstring("swfl")){
      Fswfl = sf_output("swfl");  /* scattered wavefield*/
      born_para.outputScatteredWfl=true;
    }
    else{
      born_para.Fswfl = sf_tempfile(&(born_para.sctwflfilename),"w+");
    }

  break;
  }

  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  /*                      READ AXES                             */
  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  // from the input common to FWD and ADJ
  sf_warning("VELOCITY model axes..");
  sf_axis axVel[2];
  axVel[0] = sf_iaxa(Fvel,1);
  sf_setlabel(axVel[0],"z");
  sf_setunit(axVel[0],"m");
  if(in_para.verb) sf_raxa(axVel[0]); /* depth */

  axVel[1] = sf_iaxa(Fvel,2);
  sf_setlabel(axVel[1],"x");
  sf_setunit(axVel[1],"m");
  if(in_para.verb) sf_raxa(axVel[1]); /* lateral */

  sf_warning("DENSITY model axes..");
  sf_axis axDen[2];
  axDen[0] = sf_iaxa(Fden,1);
  sf_setlabel(axDen[0],"z");
  sf_setunit(axDen[0],"m");
  if(in_para.verb) sf_raxa(axDen[0]); /* depth */

  axDen[1] = sf_iaxa(Fden,2);
  sf_setlabel(axDen[1],"x");
  sf_setunit(axDen[1],"m");
  if(in_para.verb) sf_raxa(axDen[1]); /* lateral */

  // ====================================================
  // CHECK
  // ====================================================
  sf_warning("CHECK MODEL DIMENSIONS..");
  if ((sf_n(axDen[0])!=sf_n(axVel[0])) ||
      (sf_n(axDen[1])!=sf_n(axVel[1]))){
    sf_error("Inconsistent model dimensions!");
    exit(-1);
  }
  // ====================================================
  // ====================================================

  sf_warning("WAVELET axes..");
  sf_axis axWav[2];
  axWav[0] = sf_iaxa(Fwav,1);
  axWav[1] = sf_iaxa(Fwav,2);
  sf_setlabel(axWav[0],"shot");
  sf_setlabel(axWav[1],"time");
  sf_setunit(axWav[0],"1");
  sf_setunit(axWav[1],"s");
  if(in_para.verb){
    sf_raxa(axWav[0]); /* shot */
    sf_raxa(axWav[1]); /* time */
  }

  sf_warning("SHOT COORDINATES axes..");
  sf_axis axSou[2];
  axSou[0] = sf_iaxa(Fsou,1);
  axSou[1] = sf_iaxa(Fsou,2);
  sf_setlabel(axSou[0],"coords");
  sf_setlabel(axSou[1],"shot");
  sf_setunit(axSou[0],"1");
  sf_setunit(axSou[1],"1");
  if(in_para.verb) {
    sf_raxa(axSou[0]); /* shot */
    sf_raxa(axSou[1]); /* coords */
  }

  sf_warning("RECEIVER COORDINATES axes..");
  sf_axis axRec[2];
  axRec[0] = sf_iaxa(Frec,1);
  axRec[1] = sf_iaxa(Frec,2);
  sf_setlabel(axRec[0],"coords");
  sf_setlabel(axRec[1],"receiver");
  sf_setunit(axRec[0],"1");
  sf_setunit(axRec[1],"1");
  if(in_para.verb) {
    sf_raxa(axRec[0]); /* coords */
    sf_raxa(axRec[1]); /* shots */
  }

  sf_axis axVelPert[2];
  sf_axis axDenPert[2];
  sf_axis axScData[2];
  if (in_para.adj==FWD){
    // FWD BORN OPERATOR

    if (Fvpert){
      sf_warning("VELOCITY PERTURBATION model axes..");
      axVelPert[0] = sf_iaxa(Fvpert,1);
      axVelPert[1] = sf_iaxa(Fvpert,2);
      if(in_para.verb){
        sf_raxa(axVelPert[0]); /* depth */
        sf_raxa(axVelPert[1]); /* lateral */
      }
      sf_warning("CHECK MODEL DIMENSIONS..");
      if ((sf_n(axVel[0])!=sf_n(axVelPert[0]))||
          (sf_n(axVel[1])!=sf_n(axVelPert[1]))){
        sf_error("Inconsistent model dimensions!");
        exit(-1);
      }

    }

    if (Frpert){
      sf_warning("DENSITY PERTURBATION model axes..");
      axDenPert[0] = sf_iaxa(Frpert,1);
      axDenPert[1] = sf_iaxa(Frpert,2);
      if(in_para.verb){
        sf_raxa(axDenPert[0]); /* depth */
        sf_raxa(axDenPert[1]); /* lateral */
      }

      sf_warning("CHECK MODEL DIMENSIONS..");
      if ((sf_n(axDen[0])!=sf_n(axDenPert[0]))||
          (sf_n(axDen[1])!=sf_n(axDenPert[1]))){
        sf_error("Inconsistent model dimensions!");
        exit(-1);
      }
    }

  }
  else{
    // ADJ BORN OPERATOR
    sf_warning("SCATTERED DATA axes..");
    axScData[0] = sf_iaxa(Fsdat,1);
    axScData[1] = sf_iaxa(Fsdat,2);

    if ((sf_n(axScData[0])!=sf_n(axRec[1]))){
      sf_error("Inconsistent receiver dimensions!");
      exit(-1);
    }

  }

  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  /*                       PREPARE STRUCTURES                   */
  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  if (in_para.verb) sf_warning("Allocate structures..");
  wfl_struct_t *wfl = calloc(1,sizeof(wfl_struct_t));
  acq_struct_t *acq = calloc(1,sizeof(acq_struct_t));
  mod_struct_t *mod = calloc(1,sizeof(mod_struct_t));

  // PREPARE THE MODEL PARAMETERS CUBES
  if (in_para.verb) sf_warning("Model parameter cubes..");
  prepare_born_model_2d(mod,axVel,axDen,Fvel,Fden,Fvpert,Frpert,in_para);

  // PREPARE THE ACQUISITION STRUCTURE
  if (in_para.verb) sf_warning("Prepare the acquisition geometry structure..");
  prepare_born_acquisition_2d(acq,axSou,axRec,axWav,Fsou,Frec,Fwav,Fsdat,in_para);

  // PREPARATION OF THE WAVEFIELD STRUCTURE
  if (in_para.verb) sf_warning("Prepare the wavefields for modeling..");
  prepare_born_wfl_2d(wfl,mod,Fbdat,Fbwfl,Fsdat,Fswfl,in_para);

  if (in_para.verb) sf_warning("Prepare the absorbing boundary..");
  setupABC(wfl);
  if (in_para.verb) sf_warning("Prepare the interpolation coefficients for source and receivers..");
  set_sr_interpolation_coeffs(acq,wfl);

  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  /*                       SET-UP HEADERS                       */
  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  sf_axis axTimeWfl = sf_maxa(acq->ntsnap,
                              acq->ot,
                              acq->dt*in_para.jsnap);
  sf_setlabel(axTimeWfl,"time");
  sf_setunit(axTimeWfl,"s");
  sf_axis axTimeData = sf_maxa( acq->ntdat,
                                acq->ot,
                                acq->dt);
  sf_setlabel(axTimeData,"time");
  sf_setunit(axTimeData,"s");
  if (in_para.adj==FWD){
    // WAVEFIELD HEADERS
    if (born_para.outputBackgroundWfl){
      sf_oaxa(Fbwfl,axVel[0],1);
      sf_oaxa(Fbwfl,axVel[1],2);
      sf_oaxa(Fbwfl,axTimeWfl,3);
    }
    if (born_para.outputScatteredWfl){
      sf_oaxa(Fswfl,axVel[0],1);
      sf_oaxa(Fswfl,axVel[1],2);
      sf_oaxa(Fswfl,axTimeWfl,3);
    }
    // DATA HEADERS
    if (born_para.outputBackgroundData){
        sf_oaxa(Fbdat,axRec[1],1);
        sf_oaxa(Fbdat,axTimeData,2);
    }
    // not optional
    sf_oaxa(Fsdat,axRec[1],1);
    sf_oaxa(Fsdat,axTimeData,2);

  }
  else{
    // WAVEFIELD HEADERS
    if (born_para.outputBackgroundWfl){
      sf_oaxa(Fbwfl,axVel[0],1);
      sf_oaxa(Fbwfl,axVel[1],2);
      sf_oaxa(Fbwfl,axTimeWfl,3);
    }
    if (born_para.outputScatteredWfl){
      sf_oaxa(Fswfl,axVel[0],1);
      sf_oaxa(Fswfl,axVel[1],2);
      sf_oaxa(Fswfl,axTimeWfl,3);
    }
    // DATA HEADERS
    if (born_para.outputBackgroundData){
        sf_oaxa(Fbdat,axRec[1],1);
        sf_oaxa(Fbdat,axTimeData,2);
    }

    sf_axis axPert[2];
    axPert[0] = sf_maxa(sf_n(axVel[0]),
                        sf_o(axVel[0]),
                        sf_d(axVel[0]));
    axPert[1] = sf_maxa(sf_n(axVel[1]),
                        sf_o(axVel[1]),
                        sf_d(axVel[1]));

    if (born_para.outputVelPertImage){
      sf_oaxa(Fvpert,axPert[0],1);
      sf_oaxa(Fvpert,axPert[1],2);
    }
    if (born_para.outputDenPertImage){
      sf_oaxa(Frpert,axPert[0],1);
      sf_oaxa(Frpert,axPert[1],2);
    }

  }

  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  /*                  BACKGROUND WAVEFIELD                      */
  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  if (in_para.verb) sf_warning("Background wavefield extrapolation..");
  start_bck_t=clock();
  bornbckwfl2d(wfl,acq,mod,born_para);
  end_bck_t = clock();
  total_bck_t = (float)(end_bck_t - start_bck_t) / CLOCKS_PER_SEC;

  //rewind the source wavefield
  if (born_para.outputBackgroundWfl)
    sf_seek(wfl->Fwfl,0,SEEK_SET);
  else
    fseek(born_para.Fbwfl,0, SEEK_SET);

  // reset the wavefields
  reset_wfl(wfl);

  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  /*                  BORN OPERATOR                             */
  /*------------------------------------------------------------*/
  /*------------------------------------------------------------*/
  if (in_para.adj==FWD){
    start_fwd_t=clock();
    // FWD BORN MODELING: model pert -> wfl
    if (in_para.verb) sf_warning("FWD Born operator..");

    // prepare the born sources
    if (in_para.verb) sf_warning("\tMake secondary sources..");
    start_vsrc_t=clock();
    make_born_velocity_sources_2d(wfl,mod,acq,&born_para);
    end_vsrc_t=clock();
    total_vsrc_t = (float)(end_vsrc_t - start_vsrc_t) / CLOCKS_PER_SEC;
    start_psrc_t=clock();
    make_born_pressure_sources_2d(wfl,mod,acq,&born_para);
    end_psrc_t=clock();
    total_psrc_t = (float)(end_psrc_t - start_psrc_t) / CLOCKS_PER_SEC;

    // extrapolate secondary sources
    if (in_para.verb) sf_warning("\tExtrapolate scattered wavefield..");
    start_bextrap_t=clock();
    bornfwdextrap2d(wfl,acq,mod);
    end_bextrap_t=clock();
    total_bextrap_t = (float)(end_bextrap_t - start_bextrap_t) / CLOCKS_PER_SEC;

    end_fwd_t = clock();
    total_fwd_t = (float)(end_fwd_t - start_fwd_t) / CLOCKS_PER_SEC;
  }
  else{
    start_adj_t=clock();
    // ADJ BORN MODELING: wfl -> model pert
    if (in_para.verb) sf_warning("Adjoint Born operator..");

    // extrapolate data
    if (in_para.verb) sf_warning("\tExtrapolate scattered wavefield..");
    start_bextrap_t=clock();
    bornadjextrap2d(wfl,acq,mod,&born_para);
    end_bextrap_t=clock();
    total_bextrap_t = (float)(end_bextrap_t - start_bextrap_t) / CLOCKS_PER_SEC;

    //rewind the scattered wavefield
    if (born_para.outputScatteredWfl)
      sf_seek(wfl->Fswfl,0,SEEK_SET);
    else
      fseek(born_para.Fswfl,0,SEEK_SET);

    if (in_para.verb) sf_warning("\tMake secondary sources..");
    // prepare the born sources
    start_vsrc_t=clock();
    make_born_velocity_sources_2d(wfl,mod,acq,&born_para);
    end_vsrc_t=clock();
    total_vsrc_t = (float)(end_vsrc_t - start_vsrc_t) / CLOCKS_PER_SEC;
    start_psrc_t=clock();
    make_born_pressure_sources_2d(wfl,mod,acq,&born_para);
    end_psrc_t=clock();
    total_psrc_t = (float)(end_psrc_t - start_psrc_t) / CLOCKS_PER_SEC;

    if (in_para.verb) sf_warning("\tStacking..");
    // stack wavefields
    start_vstk_t=clock();
    stack_velocity_part_2d(wfl,mod,acq,&born_para);
    end_vstk_t=clock();
    total_vstk_t = (float)(end_vstk_t - start_vstk_t) / CLOCKS_PER_SEC;
    start_pstk_t=clock();
    stack_pressure_part_2d(Fvpert,Frpert,wfl,mod,acq,&born_para);
    end_pstk_t=clock();
    total_pstk_t = (float)(end_pstk_t - start_pstk_t) / CLOCKS_PER_SEC;

    end_adj_t = clock();
    total_adj_t = (float)(end_adj_t - start_adj_t) / CLOCKS_PER_SEC;
  }


  /* -------------------------------------------------------------*/
  /* -------------------------------------------------------------*/
  /*                            FREE MEMORY                       */
  /* -------------------------------------------------------------*/
  /* -------------------------------------------------------------*/
  if (in_para.verb) sf_warning("Free memory..");
  if (in_para.verb) sf_warning("Wavefields structure..");
  clear_born_wfl_2d(wfl);
  free(wfl);
  if (in_para.verb) sf_warning("Acquisition structure..");
  clear_acq_2d(acq);
  free(acq);

  if (in_para.verb) sf_warning("Model structure..");
  clear_born_model_2d(mod);
  free(mod);

  if (in_para.verb) sf_warning("Temporary files..");
  if (!born_para.outputBackgroundWfl) remove(born_para.bckwflfilename);
  if (!born_para.outputScatteredWfl) remove(born_para.sctwflfilename);
  if (born_para.outputDenPertImage){
    remove(born_para.pv1wflfilename);
    remove(born_para.pv2wflfilename);
  }

  /* -------------------------------------------------------------*/
  /* -------------------------------------------------------------*/
  /*                   CLOSE FILES AND EXIT                       */
  /* -------------------------------------------------------------*/
  /* -------------------------------------------------------------*/
  if (Fwav!=NULL) sf_fileclose(Fwav);
  if (Fvpert!=NULL) sf_fileclose(Fvpert);
  if (Frpert!=NULL) sf_fileclose(Frpert);
  if (Fsou!=NULL) sf_fileclose(Fsou);
  if (Frec!=NULL) sf_fileclose(Frec);
  if (Fvel!=NULL) sf_fileclose(Fvel);
  if (Fden!=NULL) sf_fileclose(Fden);
  if (Fbdat!=NULL) sf_fileclose(Fbdat);
  if (Fsdat!=NULL) sf_fileclose(Fsdat);
  if (Fbwfl!=NULL) sf_fileclose(Fbwfl);
  if (Fswfl!=NULL) sf_fileclose(Fswfl);

  if (in_para.verb){
    sf_warning("=========================================================== ");
    sf_warning("PROFILING: [CPU time] ");
    sf_warning("=========================================================== ");
    sf_warning("Background wavefield extrapolation :  %7.3g [s]", total_bck_t );
    if (in_para.adj==FWD){
      sf_warning("Born FWD operator                  :  %7.3g [s]", total_fwd_t );
      sf_warning("Make velocity secondary sources    :  %7.3g [s]", total_vsrc_t );
      sf_warning("Make pressure secondary sources    :  %7.3g [s]", total_psrc_t );
      sf_warning("Scattered wavefield extrapolation  :  %7.3g [s]", total_bextrap_t );
    }
    else{
      sf_warning("Born ADJ operator                  : %7.3g [s]", total_adj_t );
      sf_warning("Scattered wavefield extrapolation  : %7.3g [s]", total_bextrap_t );
      sf_warning("Make velocity secondary sources    : %7.3g [s]", total_vsrc_t );
      sf_warning("Make pressure secondary sources    : %7.3g [s]", total_psrc_t );
      sf_warning("Stack particle velocity components : %7.3g [s]", total_vstk_t);
      sf_warning("Stack pressure component           : %7.3g [s]", total_pstk_t);
    }
    sf_warning("=========================================================== ");
    sf_warning("=========================================================== ");
  }


  exit (0);
}
