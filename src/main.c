/*
 *  main.c
 *  This file is part of LIME, the versatile line modeling engine
 *
 *  Copyright (C) 2006-2014 Christian Brinch
 *  Copyright (C) 2015-2017 The LIME development team
 *
 */
#include <locale.h>

#include "lime.h"
#include "gridio.h"

int silent = 0;
int defaultFuncFlags = 0;
double defaultDensyPower = DENSITY_POWER;

/* Forward declaration of functions only used in this file */
int initParImg(inputPars *par, image **img);
int main();



#ifdef FASTEXP
double EXP_TABLE_2D[128][10];
double EXP_TABLE_3D[256][2][10];
/* I've hard-wired the dimensions of these arrays, but it would be better perhaps to declare them as pointers, and calculate the dimensions with the help of the function call:
  calcFastExpRange(FAST_EXP_MAX_TAYLOR, FAST_EXP_NUM_BITS, &numMantissaFields, &lowestExponent, &numExponentsUsed)
*/
#else
double EXP_TABLE_2D[1][1]; /* nominal definitions so the fastexp.c module will compile. */
double EXP_TABLE_3D[1][1][1];
#endif

double ERF_TABLE[ERF_TABLE_SIZE];
double oneOver_i[FAST_EXP_MAX_TAYLOR+1];


int
initParImg(inputPars *par, image **img)
{
  /* Initialize par with default values, allocate space for the
     output fits images, initialize the images with default values,
     and finally call the input() routine from model.c to set both the
     par and image values.
  */

  int i,j,id,nImages;
  const double defaultAngle=-999.0;

  /* Set 'impossible' default values for mandatory parameters */
  par->radius    = 0;
  par->minScale  = 0;
  par->pIntensity= 0;
  par->sinkPoints= 0;

  /* Set default values for optional parameters */
  par->dust  	    = NULL;
  par->outputfile   = NULL;
  par->binoutputfile= NULL;
  par->gridfile     = NULL;
  par->pregrid      = NULL;
  par->restart      = NULL;
  par->gridInFile   = NULL;

  par->collPartIds  = malloc(sizeof(int)*MAX_N_COLL_PART);
  for(i=0;i<MAX_N_COLL_PART;i++) par->collPartIds[i] = 0; /* Possible values start at 1. */
  par->nMolWeights  = malloc(sizeof(double)*MAX_N_COLL_PART);
  for(i=0;i<MAX_N_COLL_PART;i++) par->nMolWeights[i] = -1.0;
  par->dustWeights  = malloc(sizeof(double)*MAX_N_COLL_PART); /* This param no longer has any effect. */
  for(i=0;i<MAX_N_COLL_PART;i++) par->dustWeights[i] = -1.0;
  par->collPartMolWeights = malloc(sizeof(double)*MAX_N_COLL_PART);
  for(i=0;i<MAX_N_COLL_PART;i++) par->collPartMolWeights[i] = -1.0;

  par->gridDensMaxValues = malloc(sizeof(*(par->gridDensMaxValues))*MAX_N_HIGH);
  par->gridDensMaxLoc    = malloc(sizeof(*(par->gridDensMaxLoc))*MAX_N_HIGH);
  for(i=0;i<MAX_N_HIGH;i++){
    par->gridDensMaxValues[i] = -1.0; /* Impossible default value. */
    for(j=0;j<DIM;j++) par->gridDensMaxLoc[i][j] = 0.0;
  }

  par->tcmb = 2.725;
  par->lte_only=0;
  par->init_lte=0;
  par->samplingAlgorithm=0;
  par->sampling=2; /* Now only accessed if par->samplingAlgorithm==0. */
  par->blend=0;
  par->antialias=1;
  par->polarization=0;
  par->nThreads = NTHREADS;
  par->nSolveIters=17;
  par->traceRayAlgorithm=0;
  par->resetRNG=0;
  par->doSolveRTE=0;

  par->gridOutFiles = malloc(sizeof(char *)*NUM_GRID_STAGES);
  for(i=0;i<NUM_GRID_STAGES;i++)
    par->gridOutFiles[i] = NULL;

  /* Allocate initial space for molecular data file names */
  par->moldatfile=malloc(sizeof(char *)*MAX_NSPECIES);
  par->girdatfile=malloc(sizeof(char *)*MAX_NSPECIES);
  for(id=0;id<MAX_NSPECIES;id++){
    par->moldatfile[id]=NULL;
    par->girdatfile[id]=NULL;
  }

  /* Allocate initial space for (non-LAMDA) collision partner names */
  par->collPartNames=malloc(sizeof(char *)*MAX_N_COLL_PART);
  for(i=0;i<MAX_N_COLL_PART;i++){
    par->collPartNames[i]=NULL;
  }

  /* Allocate initial space for output fits images */
  (*img)=malloc(sizeof(**img)*MAX_NIMAGES);
  for(i=0;i<MAX_NIMAGES;i++){
    (*img)[i].filename=NULL;
    (*img)[i].units=NULL;
  }

  /* First call to the user function which sets par, img values. Note that, as far as img is concerned, here we just want to find out how many images the user wants, so we can malloc the array properly. We call input() a second time then to get the actual per-image parameter values.
  */
  input(par, *img);

  /* If the user has provided a list of image filenames, the corresponding elements of (*img).filename will be non-NULL. Thus we can deduce the number of images from the number of non-NULL elements. */
  nImages=0;
  while((*img)[nImages].filename!=NULL && nImages<MAX_NIMAGES)
    nImages++;

  /* Set img defaults. */
  for(i=0;i<nImages;i++) {
    (*img)[i].source_vel=0.0;
    (*img)[i].phi=0.0;
    (*img)[i].nchan=0;
    (*img)[i].velres=-1.;
    (*img)[i].trans=-1;
    (*img)[i].molI=-1;
    (*img)[i].freq=-1.;
    (*img)[i].bandwidth=-1.;
    (*img)[i].incl    = defaultAngle;
    (*img)[i].azimuth = defaultAngle;
    (*img)[i].posang  = defaultAngle;
    (*img)[i].unit = 0;
    (*img)[i].doInterpolateVels = 0;
  }

  /* Second-pass reading of the user-set parameters (this time just to read the par->moldatfile and img stuff). */
  input(par,*img);

  return nImages;
}


void
run(inputPars inpars, image *inimg, const int nImages){
  /* Run LIME with inpars and the output fits files specified.

     This routine may be used as an interface to LIME from external
     programs. In this case, inpars and img must be specified by the
     external program.
  */
  int i,gi,si;
  int initime=time(0);
  int popsdone=0;
  molData *md=NULL;
  configInfo par;
  imageInfo *img=NULL;
  struct grid *gp=NULL;
  char message[80];
  int nEntries=0;
  double *lamtab=NULL,*kaptab=NULL;
  int *allUniqueCollPartIds=NULL;
  int numUniqueCollPartsFound;

  /*Set locale to avoid trouble when reading files*/
  setlocale(LC_ALL, "C");

  if(!silent) greetings();
  if(!silent) screenInfo();

#ifdef FASTEXP
  calcTableEntries(FAST_EXP_MAX_TAYLOR, FAST_EXP_NUM_BITS);
#endif
  fillErfTable();

  parseInput(inpars, inimg, nImages, &par, &img, &md); /* Sets par.numDensities for !(par.doPregrid || par.restart) */

  if(!silent && par.nThreads>1){
    sprintf(message, "Number of threads used: %d", par.nThreads);
    printMessage(message);
  }

  if(par.doPregrid){
    mallocAndSetDefaultGrid(&gp, (size_t)par.ncell, (size_t)par.nSpecies);
    predefinedGrid(&par,gp); /* Sets par.numDensities */
    checkUserDensWeights(&par); /* Needs par.numDensities */
  }else if(par.restart){
    popsin(&par,&gp,&md,&popsdone);
  }else{
    checkUserDensWeights(&par); /* Needs par.numDensities */
    readOrBuildGrid(&par,&gp);
  }

  if(par.dust != NULL)
    readDustFile(par.dust, &lamtab, &kaptab, &nEntries);

  /* Make all the continuum images:
  */
  if(par.nContImages>0){
    for(i=0;i<par.nImages;i++){
      if(!img[i].doline){
        raytrace(i, &par, gp, md, img, lamtab, kaptab, nEntries);
        writeFitsAllUnits(i, &par, img);
      }
    }
  }

  if(!popsdone)
    readMolData(&par, md, &allUniqueCollPartIds, &numUniqueCollPartsFound);

  if(par.doMolCalcs){
    if(!popsdone){
      molInit(&par, md, allUniqueCollPartIds, numUniqueCollPartsFound);
      calcGridMolDoppler(&par, md, gp);
    }
    if(par.useAbun)
      calcGridMolDensities(&par,&gp);

    for(gi=0;gi<par.ncell;gi++){
      for(si=0;si<par.nSpecies;si++)
        gp[gi].mol[si].specNumDens = malloc(sizeof(double)*md[si].nlev);
    }

    if(!popsdone && ((par.lte_only && !allBitsSet(par.dataFlags, DS_mask_populations))\
                     || par.nSolveIters>par.nSolveItersDone))
      levelPops(md, &par, gp, &popsdone, lamtab, kaptab, nEntries);

    calcGridMolSpecNumDens(&par,md,gp);

    par.nSolveItersDone = par.nSolveIters;
  }

  free(allUniqueCollPartIds);

  if(onlyBitsSet(par.dataFlags & DS_mask_all_but_mag, DS_mask_3))
    writeGridIfRequired(&par, gp, NULL, 3);
  else if(onlyBitsSet(par.dataFlags & DS_mask_all_but_mag, DS_mask_5)){
    writeGridIfRequired(&par, gp, md, 5);
  }else if(!silent){
    sprintf(message, "Data flags %x match neither mask 3 %x (cont.) or 5 %x (line).", par.dataFlags, DS_mask_3, DS_mask_5);
    warning(message);
  }

  freeSomeGridFields((unsigned int)par.ncell, (unsigned short)par.nSpecies, gp);

  /* Now make the line images.   */

  if(par.nLineImages>0){
    for(i=0;i<par.nImages;i++){
      if(img[i].doline){
        raytrace(i, &par, gp, md, img, lamtab, kaptab, nEntries);
        writeFitsAllUnits(i, &par, img);
      }
    }
  }

  if(!silent){
    if(par.nImages>0) reportOutput(img[0].filename);
    goodnight(initime);
  }

  freeGrid((unsigned int)par.ncell, (unsigned short)par.nSpecies, gp);
  freeMolData(par.nSpecies, md);
  freeImgInfo(par.nImages, img);
  freeConfigInfo(&par);

  if(par.dust != NULL){
    free(kaptab);
    free(lamtab);
  }
}

int main() {
  /* Main program for stand-alone LIME */

  inputPars par;
  image	*img = NULL;
  int nImages;

  nImages = initParImg(&par, &img);

  run(par, img, nImages);

  free(img);
  freeInputPars(&par);

  return 0;
}

