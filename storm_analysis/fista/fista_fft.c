/*
 * C library for FISTA (using FFT to compute Ax).
 *
 * Notes:
 *   1. Image size should probably be a power of 2.
 *
 * Hazen 2/16
 */

/* Include */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <fftw3.h>

#include "../sa_library/ft_math.h"


/* FISTA Structure */
typedef struct{
  int fft_size;
  int image_size;
  int number_psfs;
  double normalization;
  double time_step;
  double tk;
  double *fft_vector;
  double *image;
  double **x_vector;
  double **x_vector_old;
  double **y_vector;

  fftw_plan fft_forward;
  fftw_plan fft_backward;

  fftw_complex *Ax_fft;
  fftw_complex *fft_vector_fft;
  fftw_complex *image_fft;
  fftw_complex **psf_fft;
} fistaData;


/* Function Declarations */
void cleanup(fistaData *);
void getXVector(fistaData *, double *);
fistaData* initialize2D(double *, double, int, int);
fistaData* initialize3D(double *, double, int, int, int);
void iterate(fistaData *, double);
double l1Error(fistaData *);
double l2Error(fistaData *);
void newImage(fistaData *, double *);
void run(fistaData *, double, int);


/* Functions */

/*
 * cleanup()
 *
 * fista_data - A pointer to a fistaData structure.
 */
void cleanup(fistaData *fista_data)
{
  int i;
  
  free(fista_data->image);

  for(i=0;i<fista_data->number_psfs;i++){
    free(fista_data->x_vector[i]);
    free(fista_data->x_vector_old[i]);
    free(fista_data->y_vector[i]);
  }
  free(fista_data->x_vector);
  free(fista_data->x_vector_old);
  free(fista_data->y_vector);
    
  fftw_destroy_plan(fista_data->fft_forward);
  fftw_destroy_plan(fista_data->fft_backward);

  fftw_free(fista_data->Ax_fft);
  fftw_free(fista_data->fft_vector);
  fftw_free(fista_data->fft_vector_fft);
  fftw_free(fista_data->image_fft);

  for(i=0;i<fista_data->number_psfs;i++){
    fftw_free(fista_data->psf_fft[i]);
  }
  free(fista_data->psf_fft);
  
  free(fista_data);
}

/*
 * getXVector()
 *
 * Copies the current x vector into user supplied storage. Also
 * converts back from [(x1,y1), (x2,y2), ..] to (x,y,z).
 *
 * fista_data - A pointer to a fistaData structure.
 * data - Storage for the x vector.
 */
void getXVector(fistaData *fista_data, double *data)
{
  int i,j;
  double *t1;

  for(i=0;i<fista_data->number_psfs;i++){
    t1 = fista_data->x_vector[i];
    for(j=0;j<fista_data->image_size;j++){
      data[j*fista_data->number_psfs+i] = t1[j];
    }
  }
}

/*
 * initialize2D()
 *
 * Set things up for 2D analysis.
 *
 * psf - The psf (also x_size x y_size)
 * t_step - The time step to use.
 * x_size - Size in x of data and psf.
 * y_size - Size in y of data and psf.
 */
fistaData* initialize2D(double *psf, double t_step, int x_size, int y_size)
{
  return initialize3D(psf, t_step, x_size, y_size, 1);
}

/*
 * initialize3D()
 *
 * Set things up for 3D analysis.
 *
 * psf - The psf (x_size, y_size, z_size)
 * t_step - the time step to use.
 * x_size - Size in x of data and psf.
 * y_size - Size in y of data and psf.
 * z_size - The z dimension of the psf.
 */
fistaData* initialize3D(double *psf, double t_step, int x_size, int y_size, int z_size)
{
  int i,j;
  double t1;
  fftw_complex *t2;
  fistaData *fista_data;

  fista_data = (fistaData *)malloc(sizeof(fistaData));

  /* Initialize some variables. */
  fista_data->fft_size = x_size * (y_size/2 + 1);
  fista_data->image_size = x_size * y_size;
  fista_data->normalization = 1.0/((double)(fista_data->image_size));
  fista_data->number_psfs = z_size;
  
  /* Allocate storage. */
  fista_data->image = (double *)malloc(sizeof(double) * fista_data->image_size);

  fista_data->x_vector = (double **)malloc(sizeof(double *) * z_size);
  fista_data->x_vector_old = (double **)malloc(sizeof(double *) * z_size);
  fista_data->y_vector = (double **)malloc(sizeof(double *) * z_size);
  for(i=0;i<z_size;i++){
    fista_data->x_vector[i] = (double *)malloc(sizeof(double) * fista_data->image_size);
    fista_data->x_vector_old[i] = (double *)malloc(sizeof(double) * fista_data->image_size);
    fista_data->y_vector[i] = (double *)malloc(sizeof(double) * fista_data->image_size);
  }
  
  fista_data->fft_vector = (double *)fftw_malloc(sizeof(double) * fista_data->image_size);
  fista_data->Ax_fft = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fista_data->fft_size);
  fista_data->fft_vector_fft = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fista_data->fft_size);
  fista_data->image_fft = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fista_data->fft_size);

  fista_data->psf_fft = (fftw_complex **)malloc(sizeof(fftw_complex *) * z_size);
  for(i=0;i<z_size;i++){
    fista_data->psf_fft[i] = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fista_data->fft_size);
  }
  
  /* Create FFT plans. */
  fista_data->fft_forward = fftw_plan_dft_r2c_2d(x_size, y_size, fista_data->fft_vector, fista_data->fft_vector_fft, FFTW_MEASURE);
  fista_data->fft_backward = fftw_plan_dft_c2r_2d(x_size, y_size, fista_data->fft_vector_fft, fista_data->fft_vector, FFTW_MEASURE);

  /* 
     Compute FFTs of the psfs and save in psf_fft. 
     Note: The input psfs are indexed (x,y,z) but internally we
           use [(x1,y1), (x2,y2), ..] as this is hopefully clearer.
  */
  for(i=0;i<z_size;i++){
    for(j=0;j<fista_data->image_size;j++){
      fista_data->fft_vector[j] = psf[j*z_size+i];
    }
    ftmForward(fista_data->fft_forward, fista_data->fft_vector, fista_data->fft_vector, fista_data->image_size);
    ftmComplexCopy(fista_data->fft_vector_fft, fista_data->psf_fft[i], fista_data->fft_size);
  }
  
  /* Copy psf into x_vector for debugging. */
  if (0){
    for(i=0;i<z_size;i++){
      for(j=0;j<fista_data->image_size;j++){
	fista_data->x_vector[i][j] = psf[j*z_size+i];
      }
    }
  }

  /* Calculate optimal time step. */
  fista_data->time_step = 0.0;
  for(i=0;i<z_size;i++){
    t2 = fista_data->psf_fft[i];
    for(j=0;j<fista_data->fft_size;j++){
      t1 = t2[j][0] * t2[j][0] + t2[j][1] * t2[j][1];
      if(t1>fista_data->time_step){
	fista_data->time_step = t1;
      }
    }
  }
  
  fista_data->time_step = 1.0/(2.0*fista_data->time_step);
  printf("Optimal time step is: %f\n", fista_data->time_step);
  
  fista_data->time_step = t_step;

  return fista_data;
}

/*
 * iterate()
 *
 * Performs a single cycle of optimization.
 *
 * fista_data - A pointer to a fistaData structure.
 * lambda - l1 error term weigth.
 */
void iterate(fistaData *fista_data, double lambda)
{
  int i,j;
  double lt,new_tk,t1,t2;
  double *xv,*xv_old,*yv;
  
  /* Copy current x vector into old x vector. */
  for(i=0;i<fista_data->number_psfs;i++){
    ftmDoubleCopy(fista_data->x_vector[i], fista_data->x_vector_old[i], fista_data->image_size);
  }

  /*
   * This is the plk(y) step in the FISTA algorithm.
   */
  
  /* Compute Ax_fft (n,n). x is generic here, and does not
     implicitly refer to x_vector. */
  ftmComplexZero(fista_data->Ax_fft, fista_data->fft_size);

  for(i=0;i<fista_data->number_psfs;i++){

    /* Compute FFT of y vector for each z plane. */
    ftmForward(fista_data->fft_forward, fista_data->fft_vector, fista_data->y_vector[i], fista_data->image_size);

    /* Multiply FFT of y vector by FFT of the PSF for this z plane. */
    ftmComplexMultiplyAccum(fista_data->Ax_fft, fista_data->fft_vector_fft, fista_data->psf_fft[i], fista_data->fft_size, 0);

  }
  
  /* Compute Ax_fft - b_fft (image_fft) (n,n). */
  for(i=0;i<fista_data->fft_size;i++){
    fista_data->Ax_fft[i][0] -= fista_data->image_fft[i][0];
    fista_data->Ax_fft[i][1] -= fista_data->image_fft[i][1];
  }

  /* Compute x = y - At(Ax-b) (z,n,n). */
  for(i=0;i<fista_data->number_psfs;i++){

    // Compute inverse FFT of At(Ax-b) for each image plane.
    ftmComplexMultiply(fista_data->fft_vector_fft, fista_data->Ax_fft, fista_data->psf_fft[i], fista_data->fft_size, 1);
    ftmBackward(fista_data->fft_backward, fista_data->fft_vector_fft, fista_data->fft_vector_fft, fista_data->fft_size);

    // Update x vector.
    t1 = 2.0*fista_data->time_step*fista_data->normalization;
    xv = fista_data->x_vector[i];
    yv = fista_data->y_vector[i];
    for(j=0;j<fista_data->image_size;j++){
      xv[j] = yv[j] - t1*fista_data->fft_vector[j];
    }
  }
  
  /* Shrink x vector. */
  lt = fista_data->time_step*lambda;
  for(i=0;i<fista_data->number_psfs;i++){
    xv = fista_data->x_vector[i];
    for(j=0;j<fista_data->image_size;j++){
      t1 = xv[j];
      t2 = fabs(xv[j]) - lt;
      if (t2 <= 0.0){
	xv[j] = 0.0;
      }
      else{
	xv[j] = (t1 > 0.0) ? t2 : -t2;
      }
    }
  }

  /* 
   * Update tk term step.
   */
  new_tk = 0.5*(1.0 + sqrt(1.0 + 4.0 * fista_data->tk * fista_data->tk));

  /* 
   * Compute new y vector step.
   */
  t1 = (fista_data->tk - 1.0)/new_tk;
  for(i=0;i<fista_data->number_psfs;i++){
    xv = fista_data->x_vector[i];
    xv_old = fista_data->x_vector_old[i];
    yv = fista_data->y_vector[i];
    for(j=0;j<fista_data->image_size;j++){
      yv[j] = xv[j] + t1*(xv[j] - xv_old[j]);
    }
  }

  /* 
   * Update tk term step.
   */
  fista_data->tk = new_tk;
}

/*
 * l1Error
 *
 * fista_data - A pointer to a fistaData structure.
 * Return sum of the x vector.
 */
double l1Error(fistaData *fista_data)
{
  int i,j;
  double sum;
  double *xv;
  
  sum = 0.0;
  for(i=0;i<fista_data->number_psfs;i++){
    xv = fista_data->x_vector[i];
    for(j=0;j<fista_data->image_size;j++){
      sum += fabs(xv[j]);
    }
  }

  return sum;
}

/*
 * l2Error
 *
 * fista_data - A pointer to a fistaData structure.
 * Return the error in the fit.
 */
double l2Error(fistaData *fista_data)
{
  int i;
  double l2_error,t1;

  /* Compute Ax_fft (n,n). */
  ftmComplexZero(fista_data->Ax_fft, fista_data->fft_size);
  
  for(i=0;i<fista_data->number_psfs;i++){

    // Compute FFT of x vector for each z plane.
    ftmForward(fista_data->fft_forward, fista_data->fft_vector, fista_data->x_vector[i], fista_data->image_size);

    // Multiply FFT of x vector by FFT of the PSF for this z plane.
    ftmComplexMultiplyAccum(fista_data->Ax_fft, fista_data->fft_vector_fft, fista_data->psf_fft[i], fista_data->fft_size, 0);
  }

  /* Compute Ax. */
  ftmBackward(fista_data->fft_backward, fista_data->fft_vector_fft, fista_data->Ax_fft, fista_data->fft_size);

  /* Compute (Ax - b)^2. */
  l2_error = 0.0;
  for(i=0;i<fista_data->image_size;i++){
    t1 = fista_data->fft_vector[i] * fista_data->normalization - fista_data->image[i];
    l2_error += t1*t1;
  }

  return sqrt(l2_error);
}

/*
 * newImage()
 *
 * Initialize with a new image.
 *
 * fista_data - A pointer to a fistaData structure.
 * data - The image data.
 */
void newImage(fistaData *fista_data, double *data)
{
  int i,j;
  double *xv,*xv_old,*yv;
    
  fista_data->tk = 1.0;
  
  /* Save a copy of the image. */
  for(i=0;i<fista_data->image_size;i++){
    fista_data->image[i] = data[i];
  }
  
  /* Compute FFT of image. */
  ftmForward(fista_data->fft_forward, fista_data->fft_vector, fista_data->image, fista_data->image_size);
  ftmComplexCopy(fista_data->fft_vector_fft, fista_data->image_fft, fista_data->fft_size);

  /* Initialize x_vector, y_vectors */
  for(i=0;i<fista_data->number_psfs;i++){
    xv = fista_data->x_vector[i];
    xv_old = fista_data->x_vector_old[i];
    yv = fista_data->y_vector[i];
    for(j=0;j<fista_data->image_size;j++){
      xv[j] = 0.0;
      xv_old[j] = 0.0;
      yv[j] = 0.0;
    }
  }
}

/*
 * run()
 *
 * fista_data - A pointer to a fistaData structure.
 * Performs a fixed number of cycles at a fixed lambda.
 */
void run(fistaData *fista_data, double lambda, int cycles)
{
  int i;

  for(i=0;i<cycles;i++){
    iterate(fista_data, lambda);
  }
}


/*
 * The MIT License
 *
 * Copyright (c) 2016 Zhuang Lab, Harvard University
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
