#include "song.h"


int main(int argc, char **argv) {

  struct precision pr;        /* precision parameters (1st-order) */
  struct precision2 pr2;      /* precision parameters (2nd-order) */
  struct background ba;       /* cosmological background */
  struct thermo th;           /* thermodynamics */
  struct perturbs pt;         /* source functions (1st-order) */
  struct perturbs2 pt2;       /* source functions (2nd-order) */
  struct bessels bs;          /* bessel functions (1st-order) */
  struct bessels2 bs2;        /* bessel functions (2nd-order) */
  struct transfers tr;        /* transfer functions (1st-order) */
  struct transfers2 tr2;      /* transfer functions (2nd-order) */
  struct primordial pm;       /* primordial spectra */
  struct spectra sp;          /* output spectra (1st-order) */
  struct bispectra bi;        /* bispectra */
  struct fisher fi;           /* fisher matrix */
  struct nonlinear nl;        /* non-linear spectra */
  struct lensing le;          /* lensed spectra */
  struct output op;           /* output files */
  ErrorMsg errmsg;            /* error messages */

  /* Read parameters from input files */
  if (input_init_from_arguments(argc,argv,&pr,&ba,&th,&pt,&bs,&tr,&pm,
  &sp,&bi,&fi,&nl,&le,&op,errmsg) == _FAILURE_) {
    printf("\n\nError running input_init_from_arguments \n=>%s\n",errmsg);
    return _FAILURE_;
  }

  if (input2_init_from_arguments(argc,argv,&pr,&pr2,&ba,&th,&pt,&pt2,&bs,&bs2,&tr,&tr2,&pm,
  &sp,&bi,&fi,&nl,&le,&op,errmsg) == _FAILURE_) {
    printf("\n\nError running input_init_from_arguments \n=>%s\n",errmsg);
    return _FAILURE_;
  }

  /* This file is meant only for computations that involve second-order perturbations */
  if (pt2.has_perturbations2 == _FALSE_) {
    printf ("\n\nThe computation you requested is linear. Use 'class' rather than 'song'.\n");
    return _FAILURE_;
  }

  /* Compute background and thermodynamics */
  if (background_init(&pr,&ba) == _FAILURE_) {
    printf("\n\nError running background_init \n=>%s\n",ba.error_message);
    return _FAILURE_;
  }

  /* Compute recombination and reionisation quantities */
  if (thermodynamics_init(&pr,&ba,&th) == _FAILURE_) {
    printf("\n\nError in thermodynamics_init \n=>%s\n",th.error_message);
    return _FAILURE_;
  }

  /* Compute the first-order C_l's */
  if (compute_cls (&pr,&ba,&th,&sp,&nl,&le,errmsg) == _FAILURE_) {
    printf("\n\nError in compute_cls \n=>%s\n",errmsg);
    return _FAILURE_;
  }

  /* Compute first and second-order perturbations */
  if (perturb2_init(&pr,&pr2,&ba,&th,&pt,&pt2) == _FAILURE_) {
    printf("\n\nError in perturb2_init \n=>%s\n",pt2.error_message);
    return _FAILURE_;
  }

  /** Debug: print some stuff */
  struct perturbs2 * ppt2 = &pt2;
  if (ppt2->has_source_delta_matter == _TRUE_){
    printf("Printing matter bispectrum...\n");
    FILE * matterkernel;
    matterkernel = fopen("matterkernel.dat","w");
    int index_k1, index_k2, index_k3, index_tau, k3_size, a, last_index;
    double * pvecback = malloc(sizeof(double)* ba.bg_size);
    //    for (index_tau = 0; index_tau < ppt2->tau_size; index_tau++){
    for (index_tau = 0; index_tau < ppt2->tau_size; index_tau++){
      background_at_tau(&ba,ppt2->tau_sampling[index_tau], ba.normal_info,ba.inter_closeby,&(last_index), pvecback);
      for (index_k1 = 0; index_k1 < ppt2->k_size; index_k1++) {
        for (index_k2 = 0; index_k2 < index_k1; index_k2++) {
          k3_size = ppt2->k3_size[index_k1][index_k2];
          for (index_k3 = 0; index_k3 < k3_size; index_k3++) {
            fprintf(matterkernel, "%.16e %.16e %.16e %.16e %.16e %.16e\n",
                    pvecback[ba.index_bg_a]/ba.a_eq,
                    pvecback[ba.index_bg_H] * pvecback[ba.index_bg_a],
                    ppt2->k[index_k1],
                    ppt2->k[index_k2],
                    ppt2->k3[index_k1][index_k2][index_k3],
                    ppt2->sources[ppt2->index_tp2_delta_matter][index_k1][index_k2][index_tau*k3_size + index_k3]);

          }
        }
      }
    }
    free(pvecback);

    fclose(matterkernel);
  }
  /* Compute geometrical factors needed for the line of sight integration */
  if (bessel_init(&pr,&bs) == _FAILURE_) {
    printf("\n\nError in bessel_init \n =>%s\n",bs.error_message);
    return _FAILURE_;
  }

  if (bessel2_init(&pr,&pr2,&pt2,&bs,&bs2) == _FAILURE_) {
    printf("\n\nError in bessel2_init \n =>%s\n",bs2.error_message);
    return _FAILURE_;
  }

  /* Compute transfer functions using the line of sight integration */
  if (transfer_init(&pr,&ba,&th,&pt,&bs,&tr) == _FAILURE_) {
    printf("\n\nError in transfer_init \n=>%s\n",tr.error_message);
    return _FAILURE_;
  }

  if (transfer2_init(&pr,&pr2,&ba,&th,&pt,&pt2,&bs,&bs2,&tr,&tr2) == _FAILURE_) {
    printf("\n\nError in transfer2_init \n=>%s\n",tr2.error_message);
    return _FAILURE_;
  }

  /* Compute the primordial power spectrum of curvature perturbations */
  if (primordial_init(&pr,&pt,&pm) == _FAILURE_) {
    printf("\n\nError in primordial_init \n=>%s\n",pm.error_message);
    return _FAILURE_;
  }

  /* Compute bispectra */
  if (bispectra_init(&pr,&ba,&th,&pt,&bs,&tr,&pm,&sp,&le,&bi) == _FAILURE_) {
    printf("\n\nError in bispectra_init \n=>%s\n",bi.error_message);
    return _FAILURE_;
  }

  if (bispectra2_init(&pr,&pr2,&ba,&th,&pt,&pt2,&bs,&bs2,&tr,&tr2,&pm,&sp,&le,&bi) == _FAILURE_) {
    printf("\n\nError in bispectra2_init \n=>%s\n",bi.error_message);
    return _FAILURE_;
  }

  /* Compute Fisher matrix */
  if (fisher_init(&pr,&ba,&th,&pt,&bs,&tr,&pm,&sp,&le,&bi,&fi) == _FAILURE_) {
    printf("\n\nError in fisher_init \n=>%s\n",fi.error_message);
    return _FAILURE_;
  }

  /* Write output files */
  if (output_init(&ba,&pt,&sp,&nl,&le,&bi,&fi,&op) == _FAILURE_) {
    printf("\n\nError in output_init \n=>%s\n",op.error_message);
    return _FAILURE_;
  }

  // =================================================================================
  // =                                  Free memory                                  =
  // =================================================================================

  if (fisher_free(&bi,&fi) == _FAILURE_) {
    printf("\n\nError in fisher_free \n=>%s\n",fi.error_message);
    return _FAILURE_;
  }

  if (bispectra_free(&pr,&pt,&sp,&le,&bi) == _FAILURE_) {
    printf("\n\nError in bispectra_free \n=>%s\n",bi.error_message);
    return _FAILURE_;
  }

  if (lensing_free(&le) == _FAILURE_) {
    printf("\n\nError in lensing_free \n=>%s\n",le.error_message);
    return _FAILURE_;
  }

  if (pt.has_cls == _TRUE_) {
    if (spectra_free(&sp) == _FAILURE_) {
      printf("\n\nError in spectra_free \n=>%s\n",sp.error_message);
      return _FAILURE_;
    }
  }

  if (primordial_free(&pm) == _FAILURE_) {
    printf("\n\nError in primordial_free \n=>%s\n",pm.error_message);
    return _FAILURE_;
  }

  if (transfer2_free(&pr2,&pt2,&tr2) == _FAILURE_) {
    printf("\n\nError in transfer2_free \n=>%s\n",tr2.error_message);
    return _FAILURE_;
  }

  if (transfer_free(&tr) == _FAILURE_) {
    printf("\n\nError in transfer_free \n=>%s\n",tr.error_message);
    return _FAILURE_;
  }

  if (bessel2_free(&pr,&pr2,&bs,&bs2) == _FAILURE_)  {
    printf("\n\nError in bessel2_free \n=>%s\n",bs2.error_message);
    return _FAILURE_;
  }

  if (bessel_free(&pr,&bs) == _FAILURE_)  {
    printf("\n\nError in bessel_free \n=>%s\n",bs.error_message);
    return _FAILURE_;
  }

  if (perturb2_free(&pr2,&pt2) == _FAILURE_) {
    printf("\n\nError in perturb2_free \n=>%s\n",pt2.error_message);
    return _FAILURE_;
  }

  if (perturb_free(&pr,&pt) == _FAILURE_) {
    printf("\n\nError in perturb_free \n=>%s\n",pt.error_message);
    return _FAILURE_;
  }

  if (thermodynamics_free(&th) == _FAILURE_) {
    printf("\n\nError in thermodynamics_free \n=>%s\n",th.error_message);
    return _FAILURE_;
  }

  if (background_free(&ba) == _FAILURE_) {
    printf("\n\nError in background_free \n=>%s\n",ba.error_message);
    return _FAILURE_;
  }

  if (precision2_free(&pr2) == _FAILURE_) {
    printf("\n\nError in precision2_free \n=>%s\n",pr2.error_message);
    return _FAILURE_;
  }

  parser_free(pr.input_file_content);
  free (pr.input_file_content);

  return _SUCCESS_;

}
