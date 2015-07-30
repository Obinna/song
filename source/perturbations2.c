/** @file perturbations2.c
 *
 * Module to compute the cosmological perturbations at second
 * order and build the line of sight sources.
 *
 * The computation involves solving the Einstein-Boltzmann system of
 * coupled differential equations at second order in the cosmological
 * perturbations. Every passage is documented in the source code. For
 * a more detailed explanation of the physics behind this module,
 * please refer to chapter 3 and 4 of my thesis (http://arxiv.org/abs/1405.2280).
 * For details on the methodology, the evolver used (ndf15) and the
 * differential system, please look at chapter 5 in general and sec 5.3
 * in particular.
 *
 * Once their evolution is known, the perturbations are used to build
 * the line-of-sight sources for the photon temperature and polarisation
 * (and other observables), just as it is done at first order in CLASS.
 * These sources are the main output of the module and will be later
 * integrated in the transfer2.c module to obtain today's value of the 
 * second-order transfer functions. See sec 5.5 of my thesis (link above)
 * for details on this line-of-sight integration. The transfer functions
 * are the main ingredient to predict observables such as the CMB bispectrum
 * (chapter 6) that can be then compared with experiment results.
 *
 * The main functions that can be called externally are:
 * -# perturb2_init() to run the module; requires background_init() and
 *    thermodynamics_init()
 * -# perturb2_free() to free all the memory associated to the module.
 * 
 * If the user specified 'store_sources_to_disk=yes', the module will save the 
 * line-of-sight sources to disk after computing them, and then free the associated 
 * memory. To reload them from disk, use perturb2_load_sources_from_disk().
 * To free again the memory associated to the sources, call
 * perturb2_free_k1_level().
 *
 * Created by Guido W. Pettinari on 01.01.2011 based on perturbations.c by the
 * CLASS team (http://class-code.net/).
 * Last modified by Guido W. Pettinari on 24.07.2015.
 */


#include "perturbations2.h"

/**
 *
 * Fill all the fields in the perturbs2 structure, especially the ppt2->sources
 * array.
 * 
 * This function calls the other perturb2_XXX functions in the order needed to 
 * solve the Boltzmann-Einstein system of equations at second order (BES2) in the
 * cosmological perturbations. In the process, it also runs perturb_init()
 * to compute the first-order perturbations required to solve the second-order system,
 * and it fills the ppt2 structure so that it can be used in the subsequent modules
 * (transfer2.c, bispectra2.c, ...).
 *
 * Before calling this function, make sure that background_init() and
 * thermodynamics_init() have already been executed. 
 * 
 * Details on the physics and on the adopted method can be found in my thesis
 * (http://arxiv.org/abs/1405.2280), chapters 3 to 5. The code itself is
 * extensively documented and hopefully will give you further insight.
 *
 * In detail, this function does:
 *
 * -# Determine which line of sight sources need to be computed and their
 *    k-sampling via perturb2_indices_of_perturbs().
 *
 * -# Determine the time sampling for the sources via the function
 *    perturb2_timesampling_for_sources().
 *
 * -# Solve the system at first order via perturb_init().
 *
 * -# Allocate one workspace per thread to store temporary values related
 *    to the second-order system.
 *
 * -# Solve the Boltzmann-Einstein system at second order in Fourier space
 *    for all the desired (k1,k2,k3) configurations via perturb2_solve().
 *    In the process, build and store the line of sight sources in ppt2->sources.
 *
 * -# If requested, store to disk the content of ppt2->sources and free the array.
 * 
 */
int perturb2_init (
     struct precision * ppr,
     struct precision2 * ppr2,
     struct background * pba,
     struct thermo * pth,
     struct perturbs * ppt,
     struct perturbs2 * ppt2
     )
{

  if (ppt2->has_perturbations2 == _FALSE_) {

    if (ppt2->perturbations2_verbose > 0)
      printf("No second-order sources requested. Second-order perturbations module skipped.\n");

    return _SUCCESS_;
  }

  if (ppt2->perturbations2_verbose > 0)
    printf("Computing second-order perturbations\n");



  // ====================================================================================
  // =                              Indices and samplings                               =
  // ====================================================================================

  /** - determine which sources need to be computed and their k-sampling */

  class_call (perturb2_indices_of_perturbs(
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2),
  ppt2->error_message,
  ppt2->error_message);


  /** - determine the time sampling for the sources */
  
  class_call (perturb2_timesampling_for_sources (
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2),
    ppt2->error_message,
    ppt2->error_message);



  // =====================================================================================
  // =                             Solve first-order system                              =
  // =====================================================================================

  /** - Run the first-order perturbations module in order to:
  
    - Compute and store in ppt->quadsources the perturbations needed to solve the
      2nd-order system.
    
    - Compute and store in ppt->sources the line-of-sight sources needed to compute
      the first-order transfer functions (not the C_l's).

    The k-sampling for the first-order perturbations (ppt->k) has been already determined
    in perturb2_get_k_lists() and matches the one for the second-order sources (ppt2->k).
    Similarly, their time sampling (ppt->tau_sampling_quadsources) has been computed
    in perturb2_timesampling_for_sources().  */

  class_call (perturb_init (
                ppr,
                pba,
                pth,
                ppt),
    ppt->error_message, ppt2->error_message);
  

  /* If the user asked to output only the 1st-order transfer functions needed by the second-order module,
  then we stop here. */
  if (ppt2->has_early_transfers1_only == _TRUE_) {

    if (ppt2->perturbations2_verbose > 0)
      printf("Computed first-order early transfer functions, exiting with success.\n");
      
    return _SUCCESS_;
  }


  /* Print some info to screen */
  if (ppt2->perturbations2_verbose > 0) {
    printf(" -> computing %s2nd-order sources ",
      ppt2->rescale_quadsources == _TRUE_ ? "RESCALED " : "");

    if (ppt->gauge == newtonian)
      printf("in Newtonian gauge ");
    if (ppt->gauge == synchronous)
      printf("in synchronous gauge ");
    
    printf ("for m=");
    for (int index_m=0; index_m < (ppr2->m_size-1); ++index_m)
      printf("%d,", ppr2->m[index_m]);
    printf("%d\n", ppr2->m[ppr2->m_size-1]);
  }


  /* Apart from ppt2->sources, all the arrays needed by the subsequent modules have been filled.
  If the user requested to load the line of sight sources from disk, we can stop the execution of
  this module now without regrets. */
  if (ppr2->load_sources_from_disk == _TRUE_) {
    
    if (ppt2->perturbations2_verbose > 0)
      printf(" -> leaving perturbs2 module; line-of-sight sources will be read from disk\n");
    
    return _SUCCESS_;
  }


  // ========================================================================================
  // =                                 Allocate workspace                                   =
  // ========================================================================================

  /* Number of available omp threads (remains always one if no openmp) */
  int number_of_threads = 1;

  /* Index of each thread (reminas always zero if no openmp) */
  int thread = 0;

  /* Flag for error management inside parallel regions */
  int abort;
  
  /* Find number of threads */
  #ifdef _OPENMP
  #pragma omp parallel
  number_of_threads = omp_get_num_threads();
  #endif
  
  #ifdef _OPENMP
  if (ppt2->perturbations2_verbose > 4)
    printf("In %s: Split computation of the source functions between %d threads\n",
     __func__,number_of_threads);
  #endif

  /* Array of workspaces.  Each workspace will contain shared quantities useful for functions
  throughout the module.  Note that each set of wavemodes (k1,k2,k3) will have a
  workspace assigned. (Note that pppw2 stands for pointer of pointers to perturb2_workspaces.) */
  struct perturb2_workspace ** pppw2;
  class_alloc (pppw2, number_of_threads*sizeof(struct perturb2_workspace*), ppt2->error_message);
  

  /* Beginning of parallel region */
  abort = _FALSE_;
  #pragma omp parallel shared(ppr,ppr2,pba,pth,ppt,ppt2,pppw2) private(thread)
  {

    #ifdef _OPENMP
    thread = omp_get_thread_num();
    #endif
    
    /* Allocate arrays in workspace */
    class_alloc_parallel(
      pppw2[thread],
      sizeof(struct perturb2_workspace),
      ppt2->error_message);

    /* Initialize each workspace */
    class_call_parallel(perturb2_workspace_init(
                          ppr,
                          ppr2,
                          pba,
                          pth,
                          ppt,
                          ppt2,
                          pppw2[thread]
                          ),
      ppt2->error_message,
      ppt2->error_message);
    
  } // end of parallel region
  
  if (abort == _TRUE_) return _FAILURE_;
  

  // ===========================================================================================
  // =                           Solve 2nd-order differential system                           =
  // ===========================================================================================

  /* Beginning of parallel region */
  abort = _FALSE_;    

  /* Loops over k1, k2, k3 follow */
  #pragma omp parallel for shared (pppw2,ppr,pba,pth,ppt,ppt2,abort) private (thread) schedule (dynamic)
  for (int index_k1 = ppt2->k_size-1; index_k1 >= 0; --index_k1) {

    #ifdef _OPENMP
    thread = omp_get_thread_num();
    #endif

    if (ppt2->perturbations2_verbose > 1)
      printf ("     * computing sources for index_k1=%d of %d, k1=%g\n",
        index_k1, ppt2->k_size-1, ppt2->k[index_k1]);

    /* Allocate the k1 level of ppt2->sources, so that it can be filled by perturb2_solve() */
    class_call_parallel (perturb2_allocate_k1_level (ppt2, index_k1),
      ppt2->error_message, ppt2->error_message);

    /* IMPORTANT: we are assuming that the quadratic sources are given in a form symmetric
    under exchange of k1 and k2.  Hence, we shall solve the system only for those k2 that
    are equal to or larger than k1, which means that the cycle on k2 will stop when
    index_k2 = index_k1. */
      
    for (int index_k2 = 0; index_k2 <= index_k1; ++index_k2) {

      for (int index_k3 = 0; index_k3 < ppt2->k3_size[index_k1][index_k2]; ++index_k3) {

          class_call_parallel (perturb2_solve (
                                 ppr,
                                 ppr2,
                                 pba,
                                 pth,
                                 ppt,
                                 ppt2,
                                 index_k1,
                                 index_k2,
                                 index_k3,
                                 pppw2[thread]),
            ppt2->error_message,
            ppt2->error_message);

      }  // end of for (index_k3)

    } // end of for (index_k2)

    /* Save sources to disk for the considered k1, and free the memory associated with them.
    The next time we'll need them, we shall load them from disk. */

    if (ppr2->store_sources_to_disk == _TRUE_) {

      class_call_parallel (perturb2_store_sources_to_disk(ppt2, index_k1),
        ppt2->error_message,
        ppt2->error_message);
        
      class_call_parallel (perturb2_free_k1_level(ppt2, index_k1),
        ppt2->error_message, ppt2->error_message);
    }
    
    #pragma omp flush(abort)
    
  }  // end of for (index_k1) and parallel region
  
  if (abort == _TRUE_) return _FAILURE_;

    

  // ==================================================================================
  // =                                 Clean & exit                                   =
  // ==================================================================================

  /* Free the workspaces */
  #pragma omp parallel shared(pppw2,pba,ppt2,abort) private(thread)
  {
    #ifdef _OPENMP
    thread=omp_get_thread_num();
    #endif
    
    class_call_parallel(perturb2_workspace_free(ppt2,pba,pppw2[thread]),
      ppt2->error_message,
      ppt2->error_message);
    
  } if (abort == _TRUE_) return _FAILURE_; /* end of parallel region */

  free(pppw2);


  if (ppt2->perturbations2_verbose > 1)
    printf(" -> filled ppt2->sources with %ld values\n", ppt2->count_memorised_sources);
    
  /* Check that the number of filled values corresponds to the number of allocated space */
  if (ppr2->load_sources_from_disk == _FALSE_)
    class_test (ppt2->count_allocated_sources != ppt2->count_memorised_sources,
      ppt2->error_message,
      "there is a mismatch between allocated (%ld) and used (%ld) space!",
      ppt2->count_allocated_sources, ppt2->count_memorised_sources);

  /* Do not evaluate the subsequent modules if ppt2->has_early_transfers2_only == _TRUE_ */
  if (ppt2->has_early_transfers2_only == _TRUE_) {
    ppt->has_cls = _FALSE_;
    ppt->has_bispectra = _FALSE_;
    ppt2->has_cls = _FALSE_;
    ppt2->has_bispectra = _FALSE_;
  }

  return _SUCCESS_;
       
} // end of perturb2_init




/**
 * Allocate the k1 level of the array for the second-order line of sight sources (ppt2->sources).
 *
 * This function makes space for the line of sight sources functions; it is called before loading
 * them from disk in perturb2_load_sources_from_disk(). It can only be called after
 * perturb2_indices_of_perturbs(), perturb2_timesampling_for_sources(), perturb2_get_k_lists().
 *
 */
int perturb2_allocate_k1_level(
     struct perturbs2 * ppt2, /*<< pointer to perturbs2 structure */
     int index_k1             /*<< index in ppt2->k that we want to load the sources for  */
     // int index_m
     )
{

  /* Issue an error if ppt2->sources[index_k1] has already been allocated */
  class_test (ppt2->has_allocated_sources[index_k1] == _TRUE_,
    ppt2->error_message,
    "the index_k1=%d level of ppt2->sources is already allocated, stop to prevent error",
    index_k1);

  long int count=0;
  
  for (int index_type = 0; index_type < ppt2->tp2_size; index_type++) {

    /* Allocate only the level corresponding to the considered 'm' mode */
    // if (ppt2->corresponding_index_m[index_type] != index_m) continue;

    /* Allocate k2 level.  Note that the size of this level is smaller than ppt2->k_size,
    and depends on k1.  The reason is that we shall solve the system only for those k2's
    that are smaller than k1 (our equations are symmetrised wrt to k1<->k2) */

    class_alloc (
      ppt2->sources[index_type][index_k1],
      (index_k1+1) * sizeof(double *),
      ppt2->error_message);

    for (int index_k2 = 0; index_k2 <= index_k1; ++index_k2) {

      /* Allocate k3-tau level. Use calloc as we rely on the array to be initialised to zero. */
      class_calloc (
        ppt2->sources[index_type][index_k1][index_k2],
        ppt2->tau_size*ppt2->k3_size[index_k1][index_k2],
        sizeof(double),
        ppt2->error_message);
    
      #pragma omp atomic
      ppt2->count_allocated_sources += ppt2->tau_size*ppt2->k3_size[index_k1][index_k2];
      count += ppt2->tau_size*ppt2->k3_size[index_k1][index_k2];

    } // end of for (index_k2)
  } // end of for (index_type)

  /* Print some debug information on memory consumption */
  if (ppt2->perturbations2_verbose > 2)
    printf(" -> allocated ~ %.3g MB (%ld doubles) for index_k1=%d; the size of ppt2->sources so far is ~ %.3g MB;\n",
      count*sizeof(double)/1e6, count, index_k1, ppt2->count_allocated_sources*sizeof(double)/1e6);

  /* We succesfully allocated the k1 level of ppt2->sources */
  ppt2->has_allocated_sources[index_k1] = _TRUE_;

  return _SUCCESS_;

}


/**
 * Load the line of sight sources from disk for a given k1 value.
 *
 * The sources will be read from the file given in ppt2->sources_paths[index_k1] and stored
 * in the array ppt2->sources. Before running this function, make sure to allocate the
 * corresponding k1 level of ppt2->sources using perturb2_allocate_k1_level().
 *
 * This function is used in the transfer2.c module.
 */
int perturb2_load_sources_from_disk(
        struct perturbs2 * ppt2,
        int index_k1
        )
{
 
  /* Allocate memory to keep the line-of-sight sources */
  class_call (perturb2_allocate_k1_level(ppt2, index_k1),
    ppt2->error_message,
    ppt2->error_message);
  
  /* Open file for reading */
  class_open (ppt2->sources_files[index_k1],
    ppt2->sources_paths[index_k1], "rb",
    ppt2->error_message);

  /* Print some debug */
  if (ppt2->perturbations2_verbose > 2)
    printf("     * reading line-of-sight source for index_k1=%d from '%s' ... \n",
      index_k1, ppt2->sources_paths[index_k1]);
  
  for (int index_type = 0; index_type < ppt2->tp2_size; ++index_type) {
  
    for (int index_k2 = 0; index_k2 <= index_k1; ++index_k2) {

      int k3_size = ppt2->k3_size[index_k1][index_k2];

        int n_to_read = ppt2->tau_size*k3_size;

        int n_read = fread(
              ppt2->sources[index_type][index_k1][index_k2],
              sizeof(double),
              n_to_read,
              ppt2->sources_files[index_k1]);

        /* Read a chunk with all the time values for this set of (type,k1,k2,k3) */
        class_test( n_read != n_to_read,
          ppt2->error_message,
          "Could not read in '%s' file, read %d entries but expected %d",
          ppt2->sources_paths[index_k1], n_read, n_to_read);

        /* Update the counter for the values stored in ppt2->sources */
        #pragma omp atomic
        ppt2->count_memorised_sources += n_to_read;

    } // end of for (index_k2)
    
  } // end of for (index_type)
  
  /* Close file */
  fclose(ppt2->sources_files[index_k1]);
  
  // if (ppt2->perturbations2_verbose > 2)
  //   printf ("Done.\n");

  return _SUCCESS_; 
  
}



/**
 * Free the k1 level of the sources array ppt2->sources.
 */
int perturb2_free_k1_level(
     struct perturbs2 * ppt2,
     int index_k1
     )
{

  /* Issue an error if ppt2->sources[index_k1] has already been freed */
  class_test (ppt2->has_allocated_sources[index_k1] == _FALSE_,
    ppt2->error_message,
    "the index_k1=%d level of ppt2->sources is already free, stop to prevent error", index_k1);

  int k1_size = ppt2->k_size;

  for (int index_type = 0; index_type < ppt2->tp2_size; index_type++) {
    for (int index_k2 = 0; index_k2 <= index_k1; index_k2++)
        free(ppt2->sources[index_type][index_k1][index_k2]);
  
    free(ppt2->sources[index_type][index_k1]);
  
  } // end of for (index_type)


  /* We succesfully freed the k1 level of ppt2->sources */
  ppt2->has_allocated_sources[index_k1] = _FALSE_;

  return _SUCCESS_;

}








/**
 *
 * Initialize indices and arrays in the second-order perturbations structure.
 *
 * In detail, this function does:
 *
 *  -# Determine what source terms need to be computed and assign them the ppt2->index_tp2_XXX indices.
 *
 *  -# Allocate and fill the second-order k-sampling arrays (ppt2->k and ppt2->k3[index_k1,index_k2])
 *     by calling perturb2_get_k_lists().
 *
 *  -# Set the first-order k-sampling to match the second-order one (ppt->k = ppt2->k).
 *
 *  -# Fill the multipole arrays (ppt2->lm_array, ppt2->lm_array_quad, ...) used to index the Boltzmann
 *     hierarchies, by calling the perturb2_get_lm_lists().
 *
 *  -# Open the files where we will store the line of sight sources at the end of the computation.
 *
 */

int perturb2_indices_of_perturbs(
        struct precision * ppr,
        struct precision2 * ppr2,
        struct background * pba,
        struct thermo * pth,
        struct perturbs * ppt,
        struct perturbs2 * ppt2
        )
{


  // ==========================================================================
  // =                           Consistency Checks                           =
  // ==========================================================================
  
  /* We need to compute something, don't we? :-) */
  class_test (
    (ppt2->has_cmb_temperature == _FALSE_) &&
    (ppt2->has_cmb_polarization_e == _FALSE_) &&
    (ppt2->has_cmb_polarization_b == _FALSE_),
    ppt2->error_message, "please specify at least an output");

  /* Synchronous gauge not supported yet */
  class_test (ppt->gauge == synchronous,
    ppt2->error_message,
    "synchronous gauge is not supported at second order yet");

  /* Curvature at second order not supported yet. You can comment the following check
  and use a curved first order on a flat second order, but the results would be
  inconsistent. */
  class_test (pba->sgnK != 0,
    ppt2->error_message,
    "curved universe not supported at second order");

  /* E-modes and B-modes start from l=2 */
  class_test (
    (ppr2->l_max_los_p<2) || (ppr2->l_max_los_p<2),
    ppt2->error_message,
    "specify l_max_los_p>1 for polarisation");
    
  /* Check that the m-list is within bounds */
  class_test (
    (ppr2->m_max_2nd_order > ppr2->l_max_los) ||
    (ppr2->m_max_2nd_order > ppr2->l_max_g) ||
    (ppr2->m_max_2nd_order > ppr2->l_max_pol_g) ||
    (ppr2->m_max_2nd_order > ppr2->l_max_ur),
    ppt2->error_message,
    "all entries in modes_song should be between 0 and l_max.");

  /* Make sure that we compute enough first-order multipoles to solve the
  second-order system (must be below the initialisation of ppt2->lm_extra). */
  class_test ((ppr2->l_max_g+ppt2->lm_extra) > ppr->l_max_g,
    ppt2->error_message,
    "insufficient number of first-order multipoles; set 'l_max_g'\
 smaller or equal than 'l_max_g + %d'",
    ppt2->lm_extra);

  if (ppt2->has_polarization2 == _TRUE_)
    class_test ((ppr2->l_max_pol_g+ppt2->lm_extra) > ppr->l_max_pol_g,
      ppt2->error_message,
      "insufficient number of first-order multipoles; set 'l_max_pol_g'\
 smaller or equal than 'l_max_pol_g + %d'",
      ppt2->lm_extra);
    
  if (pba->Omega0_ur != 0)
    class_test ((ppr2->l_max_ur+ppt2->lm_extra) > ppr->l_max_ur,
      ppt2->error_message,
      "insufficient number of first-order multipoles; set 'l_max_ur'\
 smaller or equal than 'l_max_ur + %d'",
      ppt2->lm_extra);

  /* Make sure that the number of sources kept in the line of sight integration
  is smaller than the number of evolved 2nd-order multipoles */
  if (ppt2->has_cmb_temperature)
    class_test (ppr2->l_max_los_t > ppr2->l_max_g,
      ppt2->error_message,
      "you chose to compute more line-of-sight sources than evolved multipoles\
 at first order. Make sure that l_max_los_t is smaller or equal than l_max_g.");

  if ((ppt2->has_cmb_polarization_e == _TRUE_) || (ppt2->has_cmb_polarization_b == _TRUE_))
    class_test (ppr2->l_max_los_p > ppr2->l_max_pol_g,
      ppt2->error_message,
      "you chose to compute more line-of-sight sources than evolved multipoles\
at first order. Make sure that l_max_los_p is smaller or equal than l_max_pol_g.");

  /* Make sure that the number of quadratic sources kept in the line of sight integration
  is smaller than the number of available 1st-order multipoles */
  if (ppt2->has_cmb_temperature)
    class_test (ppr2->l_max_los_quadratic_t > ppr->l_max_g,
      ppt2->error_message,
      "you chose to compute more line-of-sight quadratic sources than evolved multipoles\
 at first order. Make sure that l_max_los_quadratic_t is smaller or equal than l_max_g.");

  if ((ppt2->has_cmb_polarization_e == _TRUE_) || (ppt2->has_cmb_polarization_b == _TRUE_))
    class_test (ppr2->l_max_los_quadratic_p > ppr->l_max_pol_g,
      ppt2->error_message,
      "you chose to compute more line-of-sight quadratic sources than evolved multipoles\
 at first order. Make sure that l_max_los_quadratic_p is smaller or equal than l_max_pol_g.");


  // ======================================================================================
  // =                              What sources to compute?                              =
  // ======================================================================================

  /* Allocate and fill the m-array, which contains the azimuthal moments to evolve.  We just copy
  it from the precision structure. */
  ppt2->m_size = ppr2->m_size;
  class_alloc (ppt2->m, ppt2->m_size*sizeof(int), ppt2->error_message);
  for (int index_m=0; index_m<ppt2->m_size; ++index_m)
    ppt2->m[index_m] = ppr2->m[index_m];

  /* Count source types and assign corresponding indices (index_type).  Also set the flags to their default
  values and count the CMB fields (temperature, E-modes, B-modes...) that are needed using index_pf.  */
  int index_type = 0;
  int index_pf = 0;
  ppt2->has_cmb = _FALSE_;
  ppt2->has_lss = _FALSE_;
  ppt2->has_source_T = _FALSE_;
  ppt2->has_source_E = _FALSE_;
  ppt2->has_source_B = _FALSE_;

  // --------------------------------------------------------------------------
  // -                       Photons temperature sources                      -
  // --------------------------------------------------------------------------
  
  if (ppt2->has_cmb_temperature == _TRUE_) {

    ppt2->has_source_T = _TRUE_;
    ppt2->has_cmb = _TRUE_;
     
    /* Find the number of sources to keep for the line of sight integration. This is determined by the
    user through 'l_max_los_t' and the m-list. */
    ppt2->n_sources_T = size_l_indexm (ppr2->l_max_los_t, ppt2->m, ppt2->m_size);

    ppt2->index_tp2_T = index_type;                  /* The first moment of the hierarchy is the monopole l=0, m=0 */
    index_type += ppt2->n_sources_T;                 /* Make space for l>0 moments of the hierarchy */

    /* Increment the number of CMB fields to consider */
    strcpy (ppt2->pf_labels[index_pf], "t");
    ppt2->field_parity[index_pf] = _EVEN_;
    ppt2->index_pf_t = index_pf++;
    
  }

	// -------------------------------------------------------------------------
  // -                      magnetic field sources                      -
  // -------------------------------------------------------------------------
  
  if (ppt2->has_magnetic_field == _TRUE_) {
  
  	
  	ppt2->n_sources_M = size_l_indexm (1, ppt2->m, ppt2->m_size);
  	
    ppt2->index_tp2_M = index_type;                  /* The first moment of the hierarchy is the monopole l=0, m=0 */
    index_type += ppt2->n_sources_M;  /* Make space for l>0 moments of the hierarchy */
	
  
  }
  
  
  // -------------------------------------------------------------------------
  // -                       Photon polarization sources                     -
  // -------------------------------------------------------------------------

  /* Assign the type indices associated to linear polarisation. We decompose
  linear polarisation in E-polarisation and B-polarisation. Both kinds of
  polarisation have vanishing monopole (l=0) and dipole (l=1), while B-modes
  also vanish for scalar modes (m=0). Nonetheless, we assign the indices
  for polarisation also to these modes. The rationale is to keep the same
  indexing strategy independently from the considered field; removing the
  monopole and dipole type-indices for polarisation would have broken such
  strategy. In the rest of the module we will simply set to zero the values of
  ppt2->sources associated to the vanishing modes (monopole and dipole for E
  and B, and scalar modes for B) */

  if ((ppt2->has_cmb_polarization_e == _TRUE_) || (ppt2->has_cmb_polarization_b == _TRUE_)) {

    ppt2->has_source_E = _TRUE_;
    ppt2->has_cmb = _TRUE_;
 
    /* Number of sources to keep for the line of sight integration */
    ppt2->n_sources_E = size_l_indexm (ppr2->l_max_los_p, ppt2->m, ppt2->m_size);
    ppt2->index_tp2_E = index_type;
    index_type += ppt2->n_sources_E;

    /* Compute the number of non-vanishing E sources for debug purposes */
    ppt2->n_nonzero_sources_E = ppt2->n_sources_E;
    if (ppr2->compute_m[0])
      ppt2->n_nonzero_sources_E -= 2;
    if (ppr2->compute_m[1])
      ppt2->n_nonzero_sources_E -= 1;

    /* Increment the number of CMB fields to consider */
    strcpy (ppt2->pf_labels[index_pf], "e");
    ppt2->field_parity[index_pf] = _EVEN_;
    ppt2->index_pf_e = index_pf++;
    
    /* Free streaming mixes the two types of polarisation (see eq. 5.101 and 5.102
    of http://arxiv.org/abs/1405.2280) in the line of sight integral. This means that
    to compute the E-mode transfer functions in the transfer2.c module, we will need
    the B-mode polarisation sources as well. The exception is when only scalar modes
    are requested, in which case there is no B-mode polarisation. */
    if (ppr2->m_max_2nd_order!=0) { /* if at least a m!=0 mode is requested */

      ppt2->has_source_B = _TRUE_;

      /* Number of sources to keep for the line of sight integration */
      ppt2->n_sources_B = size_l_indexm (ppr2->l_max_los_p, ppt2->m, ppt2->m_size);
      ppt2->index_tp2_B = index_type;
      index_type += ppt2->n_sources_B;

      /* Compute the number of non-vanishing B sources for debug purposes */
      ppt2->n_nonzero_sources_B = ppt2->n_sources_B;
      if (ppr2->compute_m[0])
        ppt2->n_nonzero_sources_B -= ppr2->l_max_los_p;
      if (ppr2->compute_m[1])
        ppt2->n_nonzero_sources_B -= 1;

      /* Increment the number of CMB fields to consider */
      strcpy (ppt2->pf_labels[index_pf], "b");
      ppt2->field_parity[index_pf] = _ODD_;
      ppt2->index_pf_b = index_pf++;
      
    } // end of if non-scalar modes
  } // end of if polarisation


  // -------------------------------------------------------------------------
  // -                              Sum up sources                           -
  // -------------------------------------------------------------------------

  /* Set the size of the sources to be stored */
  ppt2->tp2_size = index_type;
  ppt2->pf_size = index_pf;

  class_test (ppt2->pf_size > _MAX_NUM_FIELDS_,
   "exceeded maximum number of allowed fields, increase _MAX_NUM_FIELDS_ in common.h",
   ppt2->error_message)
  if (ppt2->perturbations2_verbose > 1) {
    printf ("     * will compute tp2_size=%d source terms: ", ppt2->tp2_size);
    if (ppt2->has_cmb_temperature == _TRUE_)
      printf ("T=%d ", ppt2->n_sources_T);
    if (ppt2->has_source_E == _TRUE_) {
      printf ("E=%d (%d non-zero) ", ppt2->n_sources_E, ppt2->n_nonzero_sources_E);
    }
    if (ppt2->has_source_B == _TRUE_) {
      printf ("B=%d (%d non-zero) ", ppt2->n_sources_B, ppt2->n_nonzero_sources_B);
    }
    printf ("\n");
  }  
  
  /* Allocate memory for the labels of the source types */
  class_alloc (ppt2->tp2_labels, ppt2->tp2_size*sizeof(char *), ppt2->error_message);
  for (index_type=0; index_type<ppt2->tp2_size; ++index_type)
    class_alloc (ppt2->tp2_labels[index_type], 64*sizeof(char), ppt2->error_message);

  /* Allocate the first level of the ppt2->sources array */
  class_alloc (ppt2->sources, ppt2->tp2_size * sizeof(double ***), ppt2->error_message);


  // --------------------------------------------------------------------
  // -                          Initial conditions                      -
  // --------------------------------------------------------------------
  
  /* SONG supports only adiabatic initial conditions. You can skip the following
  test but you risk to obtain unconsistent results. */

  class_test (ppt->has_ad == _FALSE_,
    ppt2->error_message,
    "SONG only supports adiabatic initial conditions; make sure ic=ad in parameter file");

  ppt2->index_ic_first_order = ppt->index_ic_ad;

  
  // ==============================================================================
  // =                            Fill (l,m) arrays                               =
  // ==============================================================================

  class_call (perturb2_get_lm_lists(
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2),
    ppt2->error_message,
    ppt2->error_message);


  // ===============================================================================
  // =                                k-sampling                                   =
  // ===============================================================================


  /* Define (k1,k2,k3) values, and allocate the ppt2->sources array accordingly */
  
  class_call (perturb2_get_k_lists(
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2),
    ppt2->error_message,
    ppt2->error_message);

// ================================================================================
  // =                          Create sources directory                            =
  // ================================================================================
  

  /* Create the files to store the source functions in */
  if ((ppr2->store_sources_to_disk == _TRUE_) || (ppr2->load_sources_from_disk == _TRUE_)) {
    
    /* We are going to store the sources in n=k_size files, one for each requested k1 */
    class_alloc (ppt2->sources_files, ppt2->k_size*sizeof(FILE *), ppt2->error_message);
    class_alloc (ppt2->sources_paths, ppt2->k_size*sizeof(char *), ppt2->error_message);

    for (int index_k1=0; index_k1<ppt2->k_size; ++index_k1) {
      
      /* The name of each sources file will have the k1 index in it */
      class_alloc (ppt2->sources_paths[index_k1], _FILENAMESIZE_*sizeof(char), ppt2->error_message);
      sprintf (ppt2->sources_paths[index_k1], "%s/sources_%03d.dat", ppt2->sources_dir, index_k1);
      
    } // end of loop on index_k1
    
    if (ppr2->store_sources_to_disk == _TRUE_)
      if (ppt2->perturbations2_verbose > 2)
        printf ("     * will create %d files to store the sources\n", ppt2->k_size);
    
  } // end of if(ppr2->store_sources_to_disk)

  
  return _SUCCESS_;

}














/**
 * Compute quantities related to the spherical harmonics projection of the
 * perturbations.
 *
 * In detail, this function does:
 *
 * -# Compute quantities arrays needed to index the (l,m) level of ppt->sources:
 *    - ppt2->m
 *    - ppt2->largest_l
 *    - ppt2->largest_l_quad
 *    - ppt2->index_monopole
 *    - ppt2->corresponding_l
 *    - ppt2->corresponding_index_m
 *    - ppt2->lm_array
 *    - ppt2->lm_array_quad
 *    - ppt2->nlm_array
 *
 * -# Compute the coupling coefficients required to build the Boltzmann equations:
 *    - ppt2->c_minus
 *    - ppt2->c_plus
 *    - ppt2->d_minus
 *    - ppt2->d_plus
 *    - ppt2->d_zero
 *    - ppt2->coupling_coefficients
 */

int perturb2_get_lm_lists (
          struct precision * ppr,
          struct precision2 * ppr2,
          struct background * pba,
          struct thermo * pth,
          struct perturbs * ppt,
          struct perturbs2 * ppt2
          )
{

  // =============================================================================================
  // =                                      Build (l,m) indexes                                  =
  // =============================================================================================

  /*  Throughout this module, we shall store/access Boltzmann hierarchies into/from
  1-dimensional arrays.  To do so we assume that the angular indices (l,m) can have
  the following values:
        0 <= l <= l_max ,   m in ppr2->m .
  In principle, the azimuthal index 'm' can assume negative values, but we shall only consider
  positive ones since, with our conventions, the two are related by a factor (-1)^m.
  We shall address arrays in the following way:
      lm_multipole = array[index_monopole + lm(l,m)]
  where the function lm(l,m) is a pre-processor macro that accesses the array
  ppt2->lm_array[l][index_m], which we fill below.  Here is an example of what lm(l,m) does
  if ppr2->m = [0,2]:
  
    lm(0,0) -> 0
    lm(1,0) -> 1
    lm(1,1) -> 2
    lm(2,0) -> 3
    lm(2,1) -> 4
    lm(2,2) -> ACCESSING UNALLOCATED MEMORY
    lm(3,0) -> 5
  
    A complication arises when indexing the first-order multipoles.  The (l,m)-th Boltzmann
  equation is sourced by terms quadratic in first-order perturbations with indices (l1,m1)
  or (l2,m2), in the following range:
      l-1 < l1 < l+1 ,   m-1 < m1 < m+1   for Newtonian gauge
      l-3 < l1 < l+3 ,   m-3 < m1 < m+3   for synchronous gauge.
  where (l,m) are the multipoles indices of the second-order perturbation.  
  We label the extra term (1 for NG, 3 for SG) as 'lm_extra', and we create an ad hoc array
  called 'lm_array_quad' to index the arrays that need to have an extended (l,m) range. Examples
  of such arrays are 
      ppw2->rotation_1, ppw2->rotation_2
  and should be accessed as
      lm_multipole = array[index_monopole + lm_quad(l,m)].
  Again, 'lm_quad(l,m)' is just a pre-processor macro linked to lm_array_quad[l][m]. Note that 
  lm_quad is computed for all m's, and not only those contained in ppt2->m. */
  

  /* Obtain the maximum 'l' that we need to index in this module. We assume the first-order
  hierarchy is always longer than the second-order one. */
  ppt2->largest_l = ppr2->l_max_g;
  ppt2->largest_l_quad = ppr->l_max_g;
  
  if (pba->has_ur == _TRUE_) {
    ppt2->largest_l      = MAX(ppt2->largest_l, ppr2->l_max_ur);
    ppt2->largest_l_quad = MAX(ppt2->largest_l_quad, ppr->l_max_ur);
  }
  if (ppt2->has_polarization2 == _TRUE_) {
    ppt2->largest_l      = MAX(ppt2->largest_l, ppr2->l_max_pol_g);
    ppt2->largest_l_quad = MAX(ppt2->largest_l_quad, ppr->l_max_pol_g);
  }

    
  /* We need the rotation factors up to l=l_max, plus a number that is 1 for 
  Newtonian gauge and 3 for synchronous gauge.  The same for the azimuthal number 'm'.
  The reason for this is that the angular dependence of the quadratic sources in
  Boltzmann equation is much more complex in synchronous gauge than in Newtonian gauge. */
  ppt2->largest_l_quad = MAX (ppt2->largest_l_quad, ppt2->largest_l + ppt2->lm_extra);


  // ------------------------------------------------------------------------------------
  // -                                   lm_array                                       -
  // ------------------------------------------------------------------------------------
   
  /* ppt2->lm_array is used to index the relativistic (l,m) hierarchies. For example,
  y[index_monopole_g + lm_array[l][index_m]]
  will return the value of the (l,m)-th multipole for the photon temperature.  Note
  that ppt2->lm_array is accessed only by the preprocessor macro lm(l,m), defined in common2.h.*/
  class_alloc (ppt2->lm_array, (ppt2->largest_l_quad+1)*sizeof(int*), ppt2->error_message);    

  for (int l=0; l<=ppt2->largest_l; ++l) {

    class_calloc (ppt2->lm_array[l],
                  ppr2->index_m_max[l]+1,
                  sizeof(int),
                  ppt2->error_message);

    /* We enter the loop only if index_m_max >= 0. This is important because all the loops
    on m in this module will cycle in this way. */
    for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

      ppt2->lm_array[l][index_m] =
        multipole2offset_l_indexm (l, ppt2->m[index_m], ppt2->m, ppt2->m_size);

      /* Debug lm_array */
      // printf ("(l,m) = (%d,%d) corresponds to an offset of %d\n",
      //   l, ppt2->m[index_m], ppt2->lm_array[l][index_m]);
        
    } // end of for (index_m)

  } // end of for (l)


  // ------------------------------------------------------------------------------------
  // -                                  nlm_array                                       -
  // ------------------------------------------------------------------------------------
  
  /* ppt2->nlm_array is used to index the massive (n,l,m) hierarchies. For example,
  y[index_monopole_cdm + lm_array[l][index_m]]
  will return the value of the (n,l,m)-th multipole for the photon temperature.  Note
  that ppt2->lm_array is accessed only by the preprocessor macro nlm(n,l,m), defined
  in common2.h. */

  /* TODO: I haven't found a good way to build this array yet. The complication arises from
  the fact that not all possible (n,l,m) are needed, so that we cannot just recycle the
  various multipole2offset routines. For the time being, I just adopt an ad-hoc way in function 
  multipole2offset_n_l_indexm. In the future, update it. */

  /* For perfect fluids such as baryons and CDM, only the n<=2 beta-moments are non-vanishing,
  hence n_max = l_max = 2 */
  int n_max = 2, l_max = 2;
  class_alloc (ppt2->nlm_array, (n_max+1)*sizeof(int**), ppt2->error_message);    
  
  for (int n=0; n <= 2; ++n) {
    
    class_alloc (ppt2->nlm_array[n], (l_max+1)*sizeof(int*), ppt2->error_message);    
    
    for (int l=0; l <= MIN(n_max,l_max); ++l) {
      
      if ((l!=n) && (l!=0)) continue;
      if ((l==0) && (n%2!=0)) continue;
      
      class_alloc (ppt2->nlm_array[n][l], ppt2->m_size*sizeof(int), ppt2->error_message);    
      
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

        ppt2->nlm_array[n][l][index_m] =
          multipole2offset_n_l_indexm (n,l,ppt2->m[index_m],l_max,ppt2->m,ppt2->m_size);

        /* Debug */
        // printf("n=%d, l=%d, m=%d => offset = %d\n", n, l, m, ppt2->nlm_array[n][l][index_m]);
        
      }
    }
  }

  // ------------------------------------------------------------------------------------
  // -                                    lm_array_quad                                 -
  // ------------------------------------------------------------------------------------
  
  /* Fill ppt2->lm_array_quad', used to index the (l,m) rotated multipoles that will be stored in
  ppw2->rotation_1 and ppw2->rotation_2.  Contrary to ppt2->lm_array, this array contains multipoles 
  up to 'l_max + lm_extra' and 'm_max + lm_extra'.
  Example: rotation_1[lm_array_quad[l][m]].
  Note that ppt2->lm_array_quad is accessed only
  by the preprocessor macro lm_quad(l,m), defined in common2.h. 
   
  TODO: given an l, there is no need to compute ppw2->rotation_1 and ppw2->rotation_2 for all allowed m's.
        Just limit this computation to m = MIN (m_max,l). */
  class_alloc (ppt2->lm_array_quad, (ppt2->largest_l_quad+1)*sizeof(int*), ppt2->error_message);
  
  for (int l=0; l<=ppt2->largest_l_quad; ++l) {
  
    class_calloc (ppt2->lm_array_quad[l], l+1, sizeof(int), ppt2->error_message);
  
    for (int m=0; m<=l; ++m)
      ppt2->lm_array_quad[l][m] = multipole2offset_l_m (l,m,l+1);
  }


  // ------------------------------------------------------------------------------------
  // -                          type-multipole correspondence                           -
  // ------------------------------------------------------------------------------------
  
  /* In the perturbation2 module a source type (index_tp2) encloses both the
  multipole (l,m) and the source type (temperature, polarization, etc). The lm_arrays we
  have defined above serve the purpose of generating an univocal index_tp2 given the
  (l,m) multipole. Here, we do the inverse operation by univocally associating to index_tp2
  the multipole indices (l,m) */

  /* For a given source type index index_tp, these arrays contain respectively the
  corresponding l and index_m */
  class_alloc (ppt2->corresponding_l, ppt2->tp2_size*sizeof(int), ppt2->error_message);
  class_alloc (ppt2->corresponding_index_m, ppt2->tp2_size*sizeof(int), ppt2->error_message);

  /* For a given source type index index_tp, this array contains the index corresponding
  to the monopole of index_tp (eg index_tp2_T for temperature, index_tp2_p for polarisation) */
  class_alloc (ppt2->index_monopole, ppt2->tp2_size*sizeof(int), ppt2->error_message);

  /* Fill the above arrays */
  for (int index_tp = 0; index_tp < ppt2->tp2_size; index_tp++) {
	    /* Initialise the result to -1 */
    int l = -1;
    int index_m = -1;
    int l_max = -1;

    /* - Photon temperature */

    if ((ppt2->has_source_T==_TRUE_)
    && (index_tp >= ppt2->index_tp2_T) && (index_tp < ppt2->index_tp2_T+ppt2->n_sources_T)) {

      /* Find the position of the monopole of the same type as index_tp */
      ppt2->index_monopole[index_tp] = ppt2->index_tp2_T;

      /* Find (l,index_m) associated with index_tp */
      l_max = ppr2->l_max_los_t;
      int lm_offset = index_tp - ppt2->index_monopole[index_tp];
      offset2multipole_l_indexm (lm_offset, l_max, ppt2->m, ppt2->m_size,
                                 &l, &index_m);
                                 
      /* Set the labels of the transfer types */
      sprintf(ppt2->tp2_labels[index_tp], "T_%d_%d",l,ppt2->m[index_m]);

      /* Some debug */
      // printf("T, index_tp=%d: lm_offset=%d -> (%d,%d), label=%s, monopole = %d\n",
      // index_tp, lm_offset, l, ppt2->m[index_m],
      // ppt2->tp2_labels[index_tp], ppt2->index_monopole[index_tp]);

    }
     // *** magnetic field ***
    else if ((ppt2->has_magnetic_field == _TRUE_) 
    && (index_tp >= ppt2->index_tp2_M) && (index_tp < ppt2->index_tp2_M+ppt2->n_sources_M)) {

      /* Find the position of the monopole of the same type as index_tp */
      ppt2->index_monopole[index_tp] = ppt2->index_tp2_M;

      /* Find (l,index_m) associated with index_tp */
      l_max = 1;
      int lm_offset = index_tp - ppt2->index_monopole[index_tp];
      offset2multipole_l_indexm (lm_offset, l_max, ppt2->m, ppt2->m_size,
                                 &l, &index_m);
                                 
      /* Set the labels of the transfer types */
      sprintf(ppt2->tp2_labels[index_tp], "M_%d_%d",l,ppt2->m[index_m]);

      /* Some debug */
      // printf("T, index_tp=%d: lm_offset=%d -> (%d,%d), label=%s, monopole = %d\n",
      // index_tp, lm_offset, l, ppt2->m[index_m],
      // ppt2->tp2_labels[index_tp], ppt2->index_monopole[index_tp]);

    }

    /* Photon E-mode polarization */

    else if ((ppt2->has_source_E==_TRUE_)
    && (index_tp >= ppt2->index_tp2_E) && (index_tp < ppt2->index_tp2_E+ppt2->n_sources_E)) {

      ppt2->index_monopole[index_tp] = ppt2->index_tp2_E;
      l_max = ppr2->l_max_los_p;
      int lm_offset = index_tp - ppt2->index_monopole[index_tp];
      offset2multipole_l_indexm (lm_offset, l_max, ppt2->m, ppt2->m_size,
                                 &l, &index_m);
                                 
      /* Set the labels of the transfer types */
      sprintf(ppt2->tp2_labels[index_tp], "E_%d_%d",l,ppt2->m[index_m]);

      /* Some debug */
      // printf("E, index_tp=%d: lm_offset=%d -> (%d,%d), label=%s, monopole = %d\n",
      //   index_tp, lm_offset, l, ppt2->m[index_m],
      //   ppt2->tp2_labels[index_tp], ppt2->index_monopole[index_tp]);

    }

    /* Photon B-mode polarization */

    else if ((ppt2->has_source_B==_TRUE_)
    && (index_tp >= ppt2->index_tp2_B) && (index_tp < ppt2->index_tp2_B+ppt2->n_sources_B)) {

      ppt2->index_monopole[index_tp] = ppt2->index_tp2_B;
      l_max = ppr2->l_max_los_p;
      int lm_offset = index_tp - ppt2->index_monopole[index_tp];
      offset2multipole_l_indexm (lm_offset, l_max, ppt2->m, ppt2->m_size,
                                 &l, &index_m);
                                 
      /* Set the labels of the transfer types */
      sprintf(ppt2->tp2_labels[index_tp], "B_%d_%d",l,ppt2->m[index_m]);

      /* Some debug */
      // printf("B, index_tp=%d: lm_offset=%d -> (%d,%d), label=%s, monopole = %d\n",
      //   index_tp, lm_offset, l, ppt2->m[index_m],
      //   ppt2->tp2_labels[index_tp], ppt2->index_monopole[index_tp]);

    }
    
    /* Check the result */
    class_test ((l>l_max) || (index_m>=ppt2->m_size) || (l<0) || (index_m<0),
      ppt2->error_message,
      "index_tp=%d: result (l,index_m)=(%d,%d) is out of bounds l=[%d,%d], index_m=[%d,%d]\n",
      index_tp, l, index_m, 0, l_max, 0, ppt2->m_size-1);    
    
    /* Write the result */
    ppt2->corresponding_l[index_tp] = l;
    ppt2->corresponding_index_m[index_tp] = index_m;
    
  } // end of for (index_tp)    

	// ======================================================================================
  // =                             Compute the C,D coefficients                           =
  // ======================================================================================
  
  /* We now compute the coupling coefficients appearing in eq. 141 of Beneke and Fidler 2010. These
  coefficients are used as a convenient way to express the second-order equations. They basically
  are just a decomposition of 3j symbols that is easy to write down and compute */

  int n_multipoles = size_l_indexm (ppt2->largest_l, ppt2->m, ppt2->m_size);

  class_alloc (ppt2->c_minus, n_multipoles*sizeof(double*), ppt2->error_message);
  class_alloc (ppt2->c_plus, n_multipoles*sizeof(double*), ppt2->error_message);
  class_alloc (ppt2->d_minus, n_multipoles*sizeof(double*), ppt2->error_message);
  class_alloc (ppt2->d_plus, n_multipoles*sizeof(double*), ppt2->error_message);
  class_alloc (ppt2->d_zero, n_multipoles*sizeof(double*), ppt2->error_message);

  for (int l=0; l <= ppt2->largest_l; ++l) {
    for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

      int m = ppr2->m[index_m];

      class_alloc (ppt2->c_minus[lm(l,m)], 3*sizeof(double), ppt2->error_message);
      class_alloc (ppt2->c_plus[lm(l,m)], 3*sizeof(double), ppt2->error_message);
      class_alloc (ppt2->d_minus[lm(l,m)], 3*sizeof(double), ppt2->error_message);
      class_alloc (ppt2->d_plus[lm(l,m)], 3*sizeof(double), ppt2->error_message);
      class_alloc (ppt2->d_zero[lm(l,m)], 3*sizeof(double), ppt2->error_message);
      
      for (int m2=-1; m2 <= 1; ++m2) {

        int m1 = m-m2;

        ppt2->c_minus[lm(l,m)][m2+1] = coupling_c_minus (l, m1, m);
        ppt2->c_plus[lm(l,m)][m2+1] = coupling_c_plus (l, m1, m);
        ppt2->d_minus[lm(l,m)][m2+1] = coupling_d_minus (l, m1, m);
        ppt2->d_plus[lm(l,m)][m2+1] = coupling_d_plus (l, m1, m);
        ppt2->d_zero[lm(l,m)][m2+1] = coupling_d_zero (l, m1, m);

        /* Some debug */
        // printf("c_minus (l=%d,m1=%d,m=%d) = %g\n", l, m1, m, ppt2->c_minus[lm(l,m)][m2+1]);
        // printf("c_plus (l=%d,m1=%d,m=%d) = %g\n", l, m1, m, ppt2->c_plus[lm(l,m)][m2+1]);
        
      } // end of for m2
    } // end of for m
  } // end of for l
  
  // ======================================================================================
  // =                          Compute the general coefficients                          =
  // ======================================================================================

  /* In general, a function P_ab in helicity space, can be decomposed in multipole space
  into its intensity (I), E-mode polarisation (E) and B-mode polarisation (B) parts according
  to eq. 2.10 of arXiv:1401.3296:
  
    P_E(l,m) - i P_B(l,m)  =  P[-+](l,m)
  
    P_E(l,m) + i P_B(l,m)  =  P[+-](l,m)
  
    P_I(l,m)  =  1/2 * (P[++]+P[--])(l,m) . 
  
  If P_ab is a product in helicity space, 
  
    P_ab = 1/2 (X_ac Y_cb + Y_ac X_cb),
  
  then its I, E and B components can be inferred, respectively, by eq. 3.9, 3.7 and 3.6 of
  the same reference:
  
    P_I(l3,m3) ->  + 0.5 * i^L * ( l1   l2 | l3  ) * (  l1   l2  | l3 )  
                                 ( 0    0  |  0  )   (  m1   m2  | m  ) 
                         * [ X_I(l1,m1) Y_I(l2,m2) + Y_I(l1,m1) X_I(l2,m2) ] 
  
    P_E(l3,m3) ->  + 1 * i^L * ( l1   l2 | l3  ) * (  l1   l2  | l3 )  
                               ( 0    2  |  2  )   (  m1   m2  | m  ) 
                       * [ X_I(l1,m1) Y_E(l2,m2) + Y_I(l1,m1) X_E(l2,m2) ]  for L even, 0 otherwise
  
    P_B(l3,m3) ->  + 1 * i^(L-1) * ( l1   l2 | l3  ) * (  l1   l2  | l3 )  
                                   ( 0    2  |  2  )   (  m1   m2  | m  ) 
                       * [ X_I(l1,m1) Y_E(l2,m2) + Y_I(l1,m1) X_E(l2,m2) ]  for L odd, 0 otherwise
  
  where: 
   * We are neglecting the first-order B-modes.
   * The symbol (    | ) denotes a Clebsch-Gordan symbol.
   * L = l3-l1-l2.
   * The indices (l1,l2,m1,m2) are summed.
   * The indices (l3,m3) are free.
   * The sums in P_I and P_E vanish for odd values of L because the intensity and E-mode fields
     have even parity. The B-mode field instead has odd parity. This property also ensures
     that both the i^L and i^(L-1) factors are real-valued.
   * With respect to arXiv:1401.3296 we have a -2 factor; this counters the fact that the expressions
     in that reference were for the quadratic term -1/2*delta*delta rather than for a generic X*Y.
     
  In what follows, we compute the general coupling coefficients given by
  
        prefactor * ( l1   l2 | l3  ) * (  l1   l2 | l3  )
                    ( 0    F  |  F  )   (  m1   m2 |  m  ) ,

  for F=0 (intensity) and F=2 (E and B-mode polarisation). 
  We store the result in the array ppt2->coupling_coefficients[index_pf][lm][l1][m1-m1_min][l2],
  where 'index_pf' refers to the considered field (I,E,B...). Note that, in terms of 3j symbols,
  the coefficients read: 
        
        prefactor * (-1)^m * (2*l+1) * ( l1   l2  l3  ) * (  l1   l2  l3  )
                                       ( 0    F   -F  )   (  m1   m2  -m  )
  
  The prefactor for the I,E,B fields can be read from the equations shown above:
  
        prefactor(I) = +1 * i^L
        prefactor(E) = +2 * i^L
        prefactor(B) = +2 * i^(L-1)
  
  where L=l3-l1-l2. For the even-parity fields (I and E) L is always even, while
  for the odd-parity ones (B) it is odd. Therefore, the prefactors are always
  real-valued. The polarisation prefactors are 2 insteaed of 1 because they get
  contributions from both E and B-modes contribute. 
  
  These coefficients are needed for the "delta_tilde" transformation that absorbs the
  redshift term of Boltzmann equation in the polarised case (see Sec. 3.10 of
  arXiv:1401.3296 and Hunag and Vernizzi 2013a). The coefficients always multiply two
  first-order perturbations (Eqs. 3.6, 3.7 and 3.9 of arXiv:1401.3296):
    delta_I(l1)*delta_I(l2) in the case of the temperature, and
    delta_I(l1)*delta_E(l2) in the case of the both E and B-modes.
  Note that there is no delta_B because we neglect the first-order B-modes.  */
  
  /* We compute the coupling coefficients only up to ppr2->l_max_los_quadratic, because
  this is the maximum multipoles we are gonna consider for the delta_tilde transformation.
  It is given by MAX(lmax_los_quadratic_t, lmax_los_quadratic_p). */
  ppt2->l1_max = ppr2->l_max_los_quadratic;
  ppt2->l2_max = ppr2->l_max_los_quadratic;

  /* Derived limits */
  int m1_min = -ppt2->l1_max;
  int m2_min = -ppt2->l2_max;
  int m1_max = ppt2->l1_max;
  int m2_max = ppt2->l2_max;
  int l1_size = ppt2->l1_max+1;
  int l2_size = ppt2->l2_max+1;
  int m1_size = m1_max-m1_min+1;
  int m2_size = m2_max-m2_min+1;
  long int counter = 0;
  
  // --------------------------------------------------------------------------------
  // -                        Allocate memory for the coefficients                  -
  // --------------------------------------------------------------------------------
  
  class_alloc (ppt2->coupling_coefficients, ppt2->pf_size*sizeof(double****), ppt2->error_message);
  
  for (int index_pf=0; index_pf < ppt2->pf_size; ++index_pf) {

    class_alloc (ppt2->coupling_coefficients[index_pf], n_multipoles*sizeof(double***), ppt2->error_message);
  
    for (int l=0; l <= ppt2->largest_l; ++l) {
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
  
        int m = ppr2->m[index_m];
  
        class_alloc (ppt2->coupling_coefficients[index_pf][lm(l,m)],
                     l1_size*sizeof(double**),
                     ppt2->error_message);
      
        for (int l1=0; l1 <= ppt2->l1_max; ++l1) {
  
          class_alloc (ppt2->coupling_coefficients[index_pf][lm(l,m)][l1],
                       m1_size*sizeof(double*),
                       ppt2->error_message);
  
          for (int m1=m1_min; m1 <= m1_max; ++m1) {

            class_calloc (ppt2->coupling_coefficients[index_pf][lm(l,m)][l1][m1-m1_min],
                         l2_size,
                         sizeof(double),
                         ppt2->error_message);
  
            counter += l2_size;        

          } // end of for m1
        } // end of for l1
      } // end of for m
    } // end of for l
  } // end of for T,E,B...
    
  if (ppt2->perturbations2_verbose > 1)
    printf ("     * allocated %ld doubles for the coupling coefficients\n", counter);
  
  /* Allocate memory for the 3J arrays. The function used to compute the 3j symbols
  returns them for all the allowed values of l2, given l and l1, and regardless of what
  is specified above for ppt2->l2_max. Since MAX(l) is ppt2->largest_l and MAX(l1)=ppt2->l1_max,
  then the triangular condition imposes MAX(l2)=ppr2->largest_l+ppt2->l1_max. */
  int l_size_max = (ppt2->largest_l + ppt2->l1_max) - abs(ppt2->largest_l - ppt2->l1_max) + 1;
  double *three_j_000, *three_j_mmm, **temp;
  class_alloc (three_j_000, l_size_max*sizeof(double), ppt2->error_message);
  class_alloc (three_j_mmm, l_size_max*sizeof(double), ppt2->error_message);
  class_alloc (temp, l_size_max*sizeof(double*), ppt2->error_message);
  for (int l2=0; l2 < l_size_max; ++l2)
    class_alloc (temp[l2], l_size_max*sizeof(double), ppt2->error_message);
  
  // ---------------------------------------------------------------------------------------
  // -                              Compute the coefficients                               -
  // ---------------------------------------------------------------------------------------
  
  for (int index_pf=0; index_pf < ppt2->pf_size; ++index_pf) {
    
    /* Determine the spin of the considered field, and the overall prefactor. These are determined using
    Eqs. 3.6, 3.7 and 3.9 of arXiv:1401.3296. We will include the sign-factor (i^L and i^(L-1)) later. */
    int F;
    double sign, prefactor;
    
    if ((ppt2->has_source_T == _TRUE_) && (index_pf == ppt2->index_pf_t)) {
      F = 0;
      prefactor = +0.5;
    }
    else if ((ppt2->has_source_E == _TRUE_) && (index_pf == ppt2->index_pf_e)) {
      F = 2;
      prefactor = +1;
    }
    else if ((ppt2->has_source_B == _TRUE_) && (index_pf == ppt2->index_pf_b)) {
      F = 2;
      prefactor = +1;
    }
    else 
      class_stop (ppt2->error_message, "mumble mumble, what is index_pf=(%d,%s)?",
        index_pf, ppt2->pf_labels[index_pf]);
      
    for (int l2=0; l2 <= ppt2->l2_max; ++l2) {
      for (int l3=0; l3 <= ppt2->largest_l; ++l3) {

        /* The absolute value of F must be always smaller than l2 and l3. This means
        that the polarisation coefficient vanishes for for l2<2 and l3<2. */
        if ((l2<abs(F)) || (l3<abs(F)))
          continue;

        for (int m1=m1_min; m1 <= m1_max; ++m1) {

          /* m1 must always be smaller than l1, whose upper limit is l2+l3 */
          if (abs(m1)>l2+l3)
            continue;

          /* Compute the following coefficients for all values of l1 and m2: 

          (-1)^m3 * (2*l3+1) * ( l1  l2  l3 ) * (  l1   l2      l3  )
                               (  0   F  -F )   (  m1   m2  -m1-m2  ) */
          int l1_min_3j, l1_max_3j;
          int m2_min_3j, m2_max_3j;

          class_call (coupling_general(
                        l2, l3, m1, F,
                        three_j_000, l_size_max,
                        three_j_mmm, l_size_max,
                        &l1_min_3j, &l1_max_3j, /* out, allowed l values */
                        &m2_min_3j, &m2_max_3j, /* out, allowed m values */
                        temp,
                        ppt2->error_message),
            ppt2->error_message,
            ppt2->error_message);

          /* Fill the coupling coefficient array, but only for the allowed values of l2 and m1 */
          for (int l1=0; l1 <= ppt2->l1_max; ++l1) {

            int L = l3-l1-l2;

            /* For even-parity fields (T and E), we skip the configurations where l1+l2+l3 is odd;
            for odd-parity (B) fields, we skip the configurations where l1+l2+l3 is even */
            if ( ((abs(L)%2==0) && (ppt2->field_parity[index_pf]==_ODD_))
              || ((abs(L)%2!=0) && (ppt2->field_parity[index_pf]==_EVEN_)) )
              continue;
            
            /* For even-parity fields, the sign-factor is i^L. For sign-parity ones, it is i^(L-1).
            In both cases, the exponent is even (see above), so that the sign-factor is real-valued */
            if (ppt2->field_parity[index_pf] == _EVEN_)
              sign = ALTERNATING_SIGN (abs(L)/2);
            else
              sign = ALTERNATING_SIGN (abs(L-1)/2);

            for (int index_m3=0; index_m3 <= ppr2->index_m_max[l3]; ++index_m3) {

              /* What we need is

              prefactor * (-1)^m3 * (2*l3+1) * ( l1  l2  l3 ) * (  l1   l2      l3  )
                                               (  0   F  -F )   (  m1   m3-m1  -m3  ) ,
        
              for all values of l1 and m3. We obtain it from what we have computed above by
              defining m2=m3-m1 => m3=m1+m2. */            
              int m3 = ppr2->m[index_m3];
              int m2 = m3-m1;

              /* Skip those configurations outside the triangular region, and those with abs(M)>L */
              if ((l1<l1_min_3j) || (l1>l1_max_3j) || (m2<m2_min_3j) || (m2>m2_max_3j))
                continue;

              ppt2->coupling_coefficients[index_pf][lm(l3,m3)][l1][m1-m1_min][l2]
                = prefactor * sign * temp[l1-l1_min_3j][m2-m2_min_3j];
            
              /* Debug - print out the values stored in ppt2->coupling_coefficients. */
              // printf ("C(l1=%d,l2=%d,l3=%d,m1=%d,m2=%d,m3=%d,F=%d)=%g\n",
              //   l1, l2, l3, m1, m2, m3, F, ppt2->coupling_coefficients[index_bf][lm(l3,m3)][l1][m1-m1_min][l2]);
            
            } // end of for m1
          } // end of for l2
        } // end of for l1
      } // end of for m
    } // end of for l
  } // end of for T,E,B...
  
  /* Free memory */
  free (three_j_000);
  free (three_j_mmm);
  for (int l2=0; l2 < l_size_max; ++l2)
    free (temp[l2]);
  free (temp);
  
  return _SUCCESS_;

} // end of perturb2_get_lm_lists




/**
 * Determine the Fourier grid in (k1,k2,k3) that will be used to sample the line of
 * sight sources.
 * 
 * At second order, the perturbations depend on three Fourier wavemodes rather than one,
 * due to mode coupling. As a result, we need to solve the Boltzmann-Einstein system on a
 * 3D grid in (k1,k2,k3).
 *
 * Here we define the (k1,k2,k3) grid using an optimised algorithm way; for an average
 * precision run, the final list will consist of about 10^6 triplets (compare to the about
 * 400 values of k at first order).
 *
 * The algorithm we use is described in sec. 5.3.2 of http://arxiv.org/abs/1405.2280; more
 * detail on mode coupling is in sec 3.5.2.
 *
 * This function will write the following fields in the ppt2 structure:
 *  - ppt2->k
 *  - ppt2->k_size
 *  - ppt2->k3
 *  - ppt2->k3_size
 *
 * and the following ones in the ppt structure:
 *  - ppt->k
 *  - ppt->k_size
 *  - ppt->k_size_cl
 *  - ppt->k_size_cmb
 *  - ppt->k_min
 *  - ppt->k_max
 */
int perturb2_get_k_lists (
          struct precision * ppr,
          struct precision2 * ppr2,
          struct background * pba,
          struct thermo * pth,
          struct perturbs * ppt,
          struct perturbs2 * ppt2
          )
{


  // ============================================================================================
  // =                              Determine grid for k1 and k2                                =
  // ============================================================================================
  
  // --------------------------------------------------------
  // -                 Logarithmic k-sampling               -
  // --------------------------------------------------------

  if (ppt2->k_sampling == log_k_sampling) {

    ppt2->k_size = ppr2->k_size_custom;
      
    class_alloc (ppt2->k, ppt2->k_size*sizeof(double), ppt2->error_message);
      
    class_call (log_space (ppt2->k, ppr2->k_min_custom, ppr2->k_max_custom, ppr2->k_size_custom),
      ppt2->error_message, ppt2->error_message);

  } // end of log sampling
  

  // --------------------------------------------------------
  // -                   Linear k-sampling                  -
  // --------------------------------------------------------

  else if (ppt2->k_sampling == lin_k_sampling) {

    ppt2->k_size = ppr2->k_size_custom;
      
    class_alloc (ppt2->k, ppt2->k_size*sizeof(double), ppt2->error_message);
      
    class_call (lin_space (ppt2->k, ppr2->k_min_custom, ppr2->k_max_custom, ppr2->k_size_custom),
      ppt2->error_message, ppt2->error_message);

  } // end of lin sampling

  
  // --------------------------------------------------------
  // -                   Class k-sampling                   -
  // --------------------------------------------------------

  /* Adopt the same k-sampling as the one adopted in first-order CLASS */
  
  else if (ppt2->k_sampling == class_sources_k_sampling) {

    class_test(ppr2->k_step_transition == 0.,
      ppt2->error_message,
      "stop to avoid division by zero");
    
    class_test(pth->rs_rec == 0.,
      ppt2->error_message,
      "stop to avoid division by zero");
  
    /* Comoving scale corresponding to sound horizon at recombination, usually of the order 2*pi/200 ~ 0.03 */
    double k_rec = 2. * _PI_ / pth->rs_rec;
  
    /* The first k-mode to be sampled is determined by k_scalar_min_tau0. Should be smaller than 2 if you want
      to compute the l=2 multipole. */
    int index_k = 0;
    double k = ppr2->k_min_tau0 / pba->conformal_age;
    index_k = 1;
  
    /* Choose a k_max corresponding to a wavelength on the last scattering surface seen today under an
    angle smaller than pi/lmax: this is equivalent to k_max*tau0 > l_max */
    double k_max = ppr2->k_max_tau0_over_l_max * ppt->l_scalar_max / pba->conformal_age;



    // *** Determine size of the k-array ***

    /* We are going to sample the k-space on a 3D grid, with \vec{k1} + \vec{k2} = \vec{k3}.
    We constrain k3 to have the range MAX(fabs(k1-k2), k_min) <= k3 <= MIN(k1+k2, k_max) where
    k_min and k_max are the limits of ppt2->k, which we determined above. */

    while (k < k_max) {
      
      /* Note that since k_rec*ppr2->k_scalar_step_transition ~ 0.03*0.2 = 0.06, we have that
      the tanh function is basically a step function of k-k_rec */
      double step = ppr2->k_step_super
                  + 0.5 * (tanh((k-k_rec)/k_rec/ppr2->k_step_transition)+1.)
                        * (ppr2->k_step_sub-ppr2->k_step_super);
  
      class_test(step * k_rec / k < ppr->smallest_allowed_variation,
        ppt2->error_message,
        "k step =%e < machine precision : leads either to numerical error or infinite loop", step * k_rec);
  
      double k_next = k + step * k_rec;
      index_k++;
      k = k_next;

    }

    ppt2->k_size = index_k;
    


    // *****   Allocate and fill ppt2->k   *****

    class_alloc (ppt2->k, ppt2->k_size*sizeof(double), ppt2->error_message);
  
    index_k = 0;
    
    ppt2->k[index_k] = ppr2->k_min_tau0/pba->conformal_age;
  
    index_k++;
  
    while (index_k < ppt2->k_size) {

      double step = ppr2->k_step_super  
           + 0.5 * (tanh((ppt2->k[index_k-1]-k_rec)/k_rec/ppr2->k_step_transition)+1.)
                 * (ppr2->k_step_sub-ppr2->k_step_super);
  
      class_test(step * k_rec / ppt2->k[index_k-1] < ppr->smallest_allowed_variation,
        ppt2->error_message,
        "k step =%e < machine precision : leads either to numerical error or infinite loop",
        step * k_rec);

      ppt2->k[index_k] = ppt2->k[index_k-1] + step * k_rec;

      index_k++;
    }
  
    /* We do not want to overshoot k_max as pbs->x_max, that is the maximum argument for which
    the Bessel functions are computed, is determined using it */
    ppt2->k[ppt2->k_size-1] = MIN (ppt2->k[ppt2->k_size-1], k_max);
  
  } // end of if(class_sources_k_sampling)


  // ----------------------------------------------------------------
  // -                        Smart k-sampling                      -
  // ----------------------------------------------------------------

  /* Adopt the same k-sampling as the one adopted in first-order CLASS, with an additional
  logarithmic sampling for very small k's in order to capture the evolution on large scales. */
  
  else if (ppt2->k_sampling == smart_sources_k_sampling) {

    class_test(ppr2->k_step_transition == 0.,
      ppt2->error_message,
      "stop to avoid division by zero");
    
    class_test(pth->rs_rec == 0.,
      ppt2->error_message,
      "stop to avoid division by zero");
  
    /* Comoving scale corresponding to sound horizon at recombination, usually of the
    order 2*pi/200 ~ 0.03 */
    double k_rec = 2 * _PI_ / pth->rs_rec;
  
    /* The first k-mode to be sampled is determined by k_scalar_min_tau0. Should be smaller
    than 2 if you want to compute the l=2 multipole. */
    int index_k = 0;
    double k = ppr2->k_min_tau0 / pba->conformal_age;
    index_k = 1;
  
    /* Choose a k_max corresponding to a wavelength on the last scattering surface seen
    today under an angle smaller than pi/lmax: this is equivalent to k_max*tau0 > l_max */
    double k_max = ppr2->k_max_tau0_over_l_max * ppt->l_scalar_max / pba->conformal_age;


    // *****   Determine size of the k-array   *****

    /* We are going to sample the k-space on a 3D grid, with \vec{k1} + \vec{k2} = \vec{k3}. We
    constrain k3 to have the range MAX(fabs(k1-k2), k_min) <= k3 <= MIN(k1+k2, k_max) where
    k_min and k_max are the limits of ppt2->k, which we determined above. */

    while (k < k_max) {
      
      /* Linear step. The tanh function is just a smooth transition between the linear and
      logarithmic regimes. When k_rec*ppr2->k_scalar_step_transition is small, we have that
      the tanh function acts as a step function of argument k-k_rec */
      double lin_step = ppr2->k_step_super
                  + 0.5 * (tanh((k-k_rec)/k_rec/ppr2->k_step_transition)+1.)
                        * (ppr2->k_step_sub-ppr2->k_step_super);
  
      /* Logarithmic step */
      double log_step = k * (ppr2->k_logstep_super - 1.);
  
      class_test(MIN(lin_step*k_rec, log_step) / k < ppr->smallest_allowed_variation,
        ppt2->error_message,
        "k step =%e < machine precision : leads either to numerical error or infinite loop",
        MIN(lin_step*k_rec, log_step));
  
      /* Use the smallest between the logarithmic and linear steps. If we are considering
      small enough scales, just use the linear step. */
      double k_next;

      if ((log_step > (lin_step*k_rec)) || (k > k_rec))
        k_next = k + lin_step * k_rec;
      else
        k_next = k * ppr2->k_logstep_super;
          
      index_k++;
      k = k_next;

    }
    int n_mult = 1;
    double spacing_fraction = 0.10;
    
    /*magnets: here is add a strange sampling in n-tuplets, which should improve the integration of at least fourier power spectra. In general, the ksampling should be compeltely reworked for this.*/
    ppt2->k_size = n_mult*(index_k-1) + 1;
    
  
    // *** Allocate and fill ppt2->k ***

    class_alloc (ppt2->k, ppt2->k_size*sizeof(double), ppt2->error_message);
  
    index_k = 0;
    
    ppt2->k[index_k] = ppr2->k_min_tau0/pba->conformal_age;
  
    index_k+= n_mult;
  
    while (index_k < ppt2->k_size) {

      /* Linear step */
      double lin_step = ppr2->k_step_super  
           + 0.5 * (tanh((ppt2->k[index_k-n_mult]-k_rec)/k_rec/ppr2->k_step_transition)+1.)
                 * (ppr2->k_step_sub-ppr2->k_step_super);
  
      /* Logarithmic step */
      double log_step = ppt2->k[index_k-n_mult] * (ppr2->k_logstep_super - 1.);
  
      /* Use the smallest between the logarithmic and linear steps. If we are considering
      small enough scales, just use the linear step. */
      if ((log_step > (lin_step*k_rec)) || (ppt2->k[index_k-n_mult] > k_rec)) {
        ppt2->k[index_k] = ppt2->k[index_k-n_mult] + lin_step * k_rec;
        // printf("linstep = %g\n", lin_step*k_rec);
      }
      else {
        ppt2->k[index_k] = ppt2->k[index_k-n_mult] * ppr2->k_logstep_super;
        // printf("logstep = %g\n", ppt2->k[index_k] - ppt2->k[index_k-1]);
      }
      
      
      index_k+= n_mult;
      
      
    }
	
    /* We do not want to overshoot k_max as pbs->x_max, that is the maximum argument for which
    the Bessel functions are computed, is determined using it */
		ppt2->k[ppt2->k_size-1] = MIN (ppt2->k[ppt2->k_size-1], k_max);
		
		// now add the tuplets
    for (int index_k=0; index_k < ppt2->k_size-n_mult; index_k+=n_mult) {  
    	double final_step = ppt2->k[index_k + n_mult] - ppt2->k[index_k];
    	for (int nc = 1; nc < n_mult; ++nc){
     	 ppt2->k[index_k+nc] = ppt2->k[index_k] + nc*final_step*spacing_fraction;
    	}
    }
  
  } // end of if(smart_sources_k_sampling)
  

  /* Some debug - print out the k-list */
   printf ("# ~~~ k-sampling for k1 and k2 (size=%d) ~~~\n", ppt2->k_size);
   for (int index_k=0; index_k < ppt2->k_size; ++index_k) {
	 	 printf ("%17d %17.7g\n", index_k, ppt2->k[index_k]);
   }
  
  
  
  
  
  // ==================================================================================
  // =                                     k3 grid                                    =
  // ==================================================================================
  
  /* Initialize counter of total k-configurations */
  ppt2->count_k_configurations = 0;
  
  /* Allocate k1 level */
  int k1_size = ppt2->k_size;

  class_alloc (ppt2->k3, k1_size*sizeof(double **), ppt2->error_message);
  class_alloc (ppt2->k3_size, k1_size*sizeof(int *), ppt2->error_message);
  if (ppt2->k3_sampling == smart_k3_sampling)
    class_alloc (ppt2->index_k3_min, k1_size*sizeof(int *), ppt2->error_message);

  for (int index_k1=0; index_k1 < ppt2->k_size; ++index_k1) {
    
    double k1 = ppt2->k[index_k1];
    
    /* Allocate k2 level */
    int k2_size = index_k1 + 1;
    
    class_alloc (ppt2->k3[index_k1], k2_size*sizeof(double *), ppt2->error_message);
    class_alloc (ppt2->k3_size[index_k1], k2_size*sizeof(int), ppt2->error_message);
    if (ppt2->k3_sampling == smart_k3_sampling)
      class_alloc (ppt2->index_k3_min[index_k1], k2_size*sizeof(int), ppt2->error_message);
    
    for (int index_k2=0; index_k2 <= index_k1; ++index_k2) {
      
      /* Remember that, given our loop choice, k2 is always smaller than k1 */
      double k2 = ppt2->k[index_k2];
      
      /* The maximum and minimum values of k3 are obtained when the values of the cosine
      between k1 and k2 are respectively +1 and -1. In an numerical code, this is not exactly
      achievable. Relying on this would ultimately give rise to nan's, for example when
      computing the Legendre polynomials or square roots (see, for instance, the definition of
      kx1 and kx2 in perturb2_geometrical_corner). Hence, we shall never sample k3 too close
      to its exact minimum or maximum. */
      double k3_min = fabs(k1 - k2) + fabs(_MIN_K3_DISTANCE_);
      double k3_max = k1 + k2 - fabs(_MIN_K3_DISTANCE_);

      /* ULTRA DIRTY MODIFICATION */
      /* The differential system dies when k1=k2 and k3 is very small. These configurations
      are irrelevant, so we set a minimum ratio between k1=k2 and k3 */
      k3_min = MAX (k3_min, (k1+k2)/_MIN_K3_RATIO_);

      /* We take k3 in the same range as k1 and k2. Comment it out if you prefer a range that
      goes all the way to the limits of the triangular condition. If you do so, remember to
      double pbs2->xx_max in input2.c */
      k3_min = MAX (k3_min, ppt2->k[0]);
      k3_max = MIN (k3_max, ppt2->k[ppt2->k_size-1]);
      
      
      /* Check that the chosen k3_min and k3_max make sense */
      class_test (k3_min >= k3_max,
        ppt2->error_message,
        "found k3_min=%g>k3_max=%g for k1(%d)=%g and k2(%d)=%g",
        k3_min, k3_max, index_k1, k1, index_k2, k2);


 			// ---------------------------------------------------------
      // -           Symmetric sampling								           -
      // ---------------------------------------------------------

      /* Here we use a grid in transformed quantities k1t,k2t and k3t in which the triangular 
      condition is trivial. Therefore we can use the full first quadrant and sample k3 as k1 */

      if ((ppt2->k3_sampling == sym_k3_sampling) ) {

        /* The size of the k3 array is the same for every (k1,k2) configuration*/
        ppt2->k3_size[index_k1][index_k2] = ppt2->k_size;
        class_alloc (ppt2->k3[index_k1][index_k2], ppt2->k_size*sizeof(double),
          ppt2->error_message);

        
        /* Build the grids */
        
         
        ppt2->k3[index_k1][index_k2] = ppt2->k;
        
      } // end of sym sampling
    

      // ---------------------------------------------------------
      // -           Linear/logarithmic sampling for k3          -
      // ---------------------------------------------------------

      /* Adopt a simple sampling with a fixed number of points for each (k1,k2) configuration.
      This is not efficient, as low values of k1 and k2 do not need a sampling as good as the
      one needed for high values */

      else if ((ppt2->k3_sampling == lin_k3_sampling) || (ppt2->k3_sampling == log_k3_sampling)) {

        /* The size of the k3 array is the same for every (k1,k2) configuration, and is read
        from the precision structure */
        ppt2->k3_size[index_k1][index_k2] = ppr2->k3_size;
        class_alloc (ppt2->k3[index_k1][index_k2], ppr2->k3_size*sizeof(double),
          ppt2->error_message);

        /* DISABLED: in this way you get a bad sampling of small k's also when using log
        sampling */
        // /* When k1=k2 we might have k3 ~ 0, which should not be included because it gives numerical problems. In this
        //   case, we set the minimum k3 to be equal to (k1+k2)/(k3_size+1), which in the case of linear
        //   sampling from k3=0 would be the second point in the grid. */
        // if (index_k2==0)
        //   k3_min = k3_max/((double)ppt2->k3_size[index_k1][index_k2] + 1.);

        /* Build the grids */
        if (ppt2->k3_sampling == log_k3_sampling) {
          log_space (ppt2->k3[index_k1][index_k2], k3_min, k3_max, ppr2->k3_size);
        }
        else if (ppt2->k3_sampling == lin_k3_sampling) {
          lin_space (ppt2->k3[index_k1][index_k2], k3_min, k3_max, ppr2->k3_size);
        }
       
      } // end of lin/log sampling


      // ---------------------------------------------------------
      // -               Linear sampling for the angle           -
      // ---------------------------------------------------------

      /* Adopt a simple sampling with a fixed number of points for each (k1,k2) configuration. This is not efficient, as
        low values of k1 and k2 do not need a sampling as good as the one needed for high values */

      else if (ppt2->k3_sampling == theta13_k3_sampling) {

        /* The size of the k3 array is the same for every (k1,k2) configuration, and is read from the
        precision structure */
        ppt2->k3_size[index_k1][index_k2] = ppr2->k3_size;
        class_alloc (ppt2->k3[index_k1][index_k2], ppr2->k3_size*sizeof(double), ppt2->error_message);

        double cosk1k2_min = (k3_min*k3_min - k1*k1 - k2*k2)/(2.*k1*k2);
        double cosk1k2_max = (k3_max*k3_max - k1*k1 - k2*k2)/(2.*k1*k2);

        class_test (fabs(cosk1k2_min)>1, ppt2->error_message, "stop to prevent nans");

        class_test (fabs(cosk1k2_max)>1, ppt2->error_message, "stop to prevent nans");

        double theta12_min = acos(cosk1k2_min);
        double theta12_max = acos(cosk1k2_max);
        double theta_step = (theta12_max - theta12_min)/(ppt2->k3_size[index_k1][index_k2]-1);
      
        if (index_k1==index_k2) {
          double angle_factor = 5.;
          theta12_min += theta_step/angle_factor;
          theta_step = (theta12_max - theta12_min)/(ppt2->k3_size[index_k1][index_k2]-1);
        }

        for (int index_k3=0; index_k3 < ppt2->k3_size[index_k1][index_k2]; ++index_k3) {

          double cosk1k2 = cos (theta12_min + index_k3*theta_step);
          
          ppt2->k3[index_k1][index_k2][index_k3] = sqrt (k1*k1 + k2*k2 + 2*cosk1k2*k1*k2);
          
        }
       
      } // end of theta13 sampling


      // ------------------------------------------------------------
      // -                   Smart sampling for k3                  -
      // ------------------------------------------------------------

      /* Adopt a sampling which is finer for large values of (k1,k2). The idea is to use the same step in k3 as the
      one used for ppt2->k.  When (k1,k2) are so small that the grid would have less than ppr2->k3_size_min, use
      ppr2->k3_size_min linearly sampled points, instead. */
  
      else if (ppt2->k3_sampling == smart_k3_sampling) {     

        /* Find the minimum allowed index of k3 inside ppt2->k. If k3_min is smaller than the smallest
        element in ppt2->k, take the latter. */
        int index_k3_min = 0;
        while (ppt2->k[index_k3_min]<k3_min) ++index_k3_min;

        /* Find the maximum allowed index of k. If k_max is larger than the largest element in ppt2->k, take the latter. */
        int index_k3_max = ppt2->k_size-1;
        while (ppt2->k[index_k3_max]>k3_max) --index_k3_max;
      
        /* Number of points in the ppt2->k grid between 'k3_min' and 'k3_max' */
        int n_triangular = index_k3_max - index_k3_min + 1;

        /* Some debug */
        // if ((index_k1==22) && (index_k2==0)) {
        //   printf("index_k3_min = %d\n", index_k3_min);
        //   printf("index_k3_max = %d\n", index_k3_max);
        //   printf("n_triangular = %d\n", n_triangular);
        // }

        /* We choose the grid to have at least 'ppr2->k3_size_min' values for every (k1,k2). If this is not possible
        using the standard ppt2->k grid, just include as many linearly sampled points between 'k3_min' and
        'k3_max' */
        if (n_triangular < ppr2->k3_size_min) {
          
          /* AS IT WAS */
          ppt2->k3_size[index_k1][index_k2] = ppr2->k3_size_min;
          class_alloc (ppt2->k3[index_k1][index_k2], ppr2->k3_size_min*sizeof(double), ppt2->error_message);
          
          /* Fill the 'k3' array with many linearly spaced grids between 'k3_min' and 'k3_max'  */
          lin_space (ppt2->k3[index_k1][index_k2],
                     k3_min,
                     k3_max,
                     ppt2->k3_size[index_k1][index_k2]);
          
          /* Flag this (k1,k2) pair to have an artificial sampling */
          ppt2->index_k3_min[index_k1][index_k2] = -1;
          
          /* MY MODIFICATIONS */
          // ppt2->k3_size[index_k1][index_k2] = 0;
          // 
          // /* Flag this (k1,k2) pair to have an artificial sampling */
          // ppt2->index_k3_min[index_k1][index_k2] = -1;
          /* END OF MY MODIFICATIONS */

        }
        /* If we do have enough points in ppt2->k to sample 'k3', just use those points */
        else {
          
          /* We add two points by hand corresponding to k3_min and k3_max, because they are most
          probably not part of ppt2->k */
          ppt2->k3_size[index_k1][index_k2] = n_triangular + 2;
          
          /* Allocate the memory for the k3-grid array */
          class_alloc (ppt2->k3[index_k1][index_k2], ppt2->k3_size[index_k1][index_k2]*sizeof(double), ppt2->error_message);
          
          /* Fill the k3-grid making sure to include k3_min and k3_max */
          ppt2->k3[index_k1][index_k2][0] = k3_min;
          
          for (int index_k3=0; index_k3 < n_triangular; ++index_k3)
            ppt2->k3[index_k1][index_k2][index_k3+1] = ppt2->k[index_k3_min+index_k3];
          
          ppt2->k3[index_k1][index_k2][ppt2->k3_size[index_k1][index_k2]-1] = k3_max;

          /* Shift the starting point of the array if we included k3_min twice */
          if (ppt2->k3[index_k1][index_k2][0] == ppt2->k3[index_k1][index_k2][1]) {
            for (int index_k3=0; index_k3 < (ppt2->k3_size[index_k1][index_k2]-1); ++index_k3)
              ppt2->k3[index_k1][index_k2][index_k3] = ppt2->k3[index_k1][index_k2][index_k3+1];
            ppt2->k3_size[index_k1][index_k2]--;
          }

          /* Decrease the size of the array if we included k3_max twice */
          int k3_size = ppt2->k3_size[index_k1][index_k2];
          if (ppt2->k3[index_k1][index_k2][k3_size-1] == ppt2->k3[index_k1][index_k2][k3_size-2])
            ppt2->k3_size[index_k1][index_k2]--;

          /* In order to not include the boundaries, comment the above and uncomment below */
          // ppt2->k3_size[index_k1][index_k2] = n_triangular;
          // class_alloc (ppt2->k3[index_k1][index_k2], ppt2->k3_size[index_k1][index_k2]*sizeof(double), ppt2->error_message);
          // for (index_k3=0; index_k3 < n_triangular; ++index_k3)
          //   ppt2->k3[index_k1][index_k2][index_k3] = ppt2->k[index_k3_min+index_k3];

        } // end of conditions of k3_size
  
      } // end of smart sampling
      
      /* Update counter of k-configurations */
      ppt2->count_k_configurations += ppt2->k3_size[index_k1][index_k2];

      /* Some debug - print out the k3 list for a special configuration */      
      // if ((index_k1==23) && (index_k2==22)) {
      // 
      //   fprintf (stderr, "k1[%d]=%.17f, k2[%d]=%.17f, k3_size=%d, k3_min=%.17f, k3_max=%.17f\n",
      //     index_k1, k1, index_k2, k2, ppt2->k3_size[index_k1][index_k2], k3_min, k3_max);
      // 
      //   for (int index_k3=0; index_k3 < ppt2->k3_size[index_k1][index_k2]; ++index_k3)
      //     fprintf(stderr, "%d %.17f\n", index_k3, ppt2->k3[index_k1][index_k2][index_k3]);
      //       
      //   fprintf (stderr, "\n\n");
      // }

    } // end of for (index_k2)

  } // end of for (index_k1)


  /* Set minimum and maximum k-values ever used in SONG */
  ppt2->k_min = ppt2->k3[0][0][0];
  ppt2->k_max = ppt2->k3[ppt2->k_size-1][ppt2->k_size-1]
                  [ppt2->k3_size[ppt2->k_size-1][ppt2->k_size-1]];
                        
  /*class_test ((ppt2->k_min==0) || (ppt2->k_max==0),
    ppt2->error_message,
    "found vanishing k value");



  // ==============================================================================================
  // =                                    First-order k-grid                                      =
  // ==============================================================================================

  /* The 1st-order system must be solved for the k-values needed at second-order. Here we set
  all the relevant k-arrays in the first-order perturbation module to match the k-sampling
  contained in ppt2->k. */
  
  int md_size = 1; /* only scalars at first supported so fare */
  int index_md_scalars = 0; /* only scalars at first supported so fare */
  
  class_alloc (ppt->k_size, md_size*sizeof(int *), ppt2->error_message);
  class_alloc (ppt->k_size_cl, md_size*sizeof(int *), ppt2->error_message);
  class_alloc (ppt->k_size_cmb, md_size*sizeof(int *), ppt2->error_message);
  class_alloc (ppt->k, md_size*sizeof(double *), ppt2->error_message);
  
  for (int index_md=0; index_md < md_size; ++index_md) {

    ppt->k_size[index_md] = ppt2->k_size;
    ppt->k_size_cl[index_md] = ppt2->k_size;
    ppt->k_size_cmb[index_md] = ppt2->k_size;

    class_alloc (ppt->k[index_md], ppt->k_size[index_md]*sizeof(double), ppt2->error_message);
    for (int index_k=0; index_k < ppt2->k_size; ++index_k)
      ppt->k[index_md][index_k] = ppt2->k[index_k];
  }
  
  /* Determine k_min and k_max. The following block is copied from perturbations.c */
  ppt->k_min = _HUGE_;
  ppt->k_max = 0.;
    if (ppt->has_scalars == _TRUE_) {
    ppt->k_min = MIN(ppt->k_min,ppt->k[index_md_scalars][0]);
    ppt->k_max = MAX(ppt->k_max,ppt->k[index_md_scalars][ppt->k_size[index_md_scalars]-1]);
  }
  if (ppt->has_vectors == _TRUE_) {
    ppt->k_min = MIN(ppt->k_min,ppt->k[ppt->index_md_vectors][0]);
    ppt->k_max = MAX(ppt->k_max,ppt->k[ppt->index_md_vectors][ppt->k_size[ppt->index_md_vectors]-1]);
  }
  if (ppt->has_tensors == _TRUE_) {
    ppt->k_min = MIN(ppt->k_min,ppt->k[ppt->index_md_tensors][0]);
    ppt->k_max = MAX(ppt->k_max,ppt->k[ppt->index_md_tensors][ppt->k_size[ppt->index_md_tensors]-1]);
  }

  /* Some debug - print first-order k-sampling */
  // for (index_k=0; index_k < ppt->k_size[ppt->index_md_scalars]; ++index_k) {
  //   printf ("%5d %10g\n", index_k, ppt->k[ppt->index_md_scalars][index_k]);
  // }

  return _SUCCESS_;

}



/**
 *
 * Define the sampling in conformal time tau for the line of sight sources and
 * for the quadratic sources.
 *
 * In detail, this function does:
 *  
 *  -# Define the time sampling for the line of sight sources in ppt2->tau_sampling.
 *     For the CMB, these are the sources that will be integrated over time in the
 *     transfer2.c module to obtain today's value of the transfer functions.
 *
 *  -# Define the time sampling for the quadratic sources in ppt->tau_sampling_quadsources,
 *     so that it can be later passed to the first-order perturbations.c module. The
 *     quadratic sources are quadratic combinations of first-order perturbations that
 *     enter the second-order differential system as sources.
 *
 *  -# Allocate the (k1, k2, k3, tau) levels of ppt2->sources, the array where we shall
 *     store the line-of-sight sources.
 *
 */

int perturb2_timesampling_for_sources (
             struct precision * ppr,
             struct precision2 * ppr2,
             struct background * pba,
             struct thermo * pth,
             struct perturbs * ppt,
             struct perturbs2 * ppt2
             )
{

  /* Temporary arrays used to store background and thermodynamics quantities */
  double *pvecback, *pvecthermo;
  class_alloc (pvecback, pba->bg_size*sizeof(double), ppt2->error_message);
  class_alloc (pvecthermo, pth->th_size*sizeof(double), ppt2->error_message);
  double a, Hc, H_prime, kappa_dot, timescale_source;
  int dump;


  // =====================================================================================
  // =                              Custom sources sampling                              =
  // =====================================================================================
  
  /* The user can specify a time sampling for the sources via the parameter file,
  by providing a start time, and end time and a sampling method (linear or
  logarithmic). */
  
  if (ppt2->has_custom_timesampling == _TRUE_) {

    ppt2->tau_size = ppt2->custom_tau_size;
    class_alloc (ppt2->tau_sampling, ppt2->tau_size*sizeof(double), ppt2->error_message);
    
    /* If the user set the custom end-time to 0, we assume that he wants to compute
    the sources all the way to today */
    if (ppt2->custom_tau_end == 0)
      ppt2->custom_tau_end = pba->conformal_age;
  
    /* Linear sampling */
    if (ppt2->custom_tau_mode == lin_tau_sampling) {
      lin_space(ppt2->tau_sampling, ppt2->custom_tau_ini, ppt2->custom_tau_end, ppt2->tau_size);
    }
    /* Logarithmic sampling */
    else if (ppt2->custom_tau_mode == log_tau_sampling) {
      log_space(ppt2->tau_sampling, ppt2->custom_tau_ini, ppt2->custom_tau_end, ppt2->tau_size);
    }
  }



  // =====================================================================================
  // =                            Standard sources sampling                              =
  // =====================================================================================

  /* Determine the time sampling of the line of sight sources using the same algorithm
  as in standard CLASS, whereby the sampling is denser where the perturbations evolve
  faster, based on the visibility function and on the Hubble rate. For details, please
  refer to CLASS function perturb_timesampling_for_sources(). With respect to that
  function, we use the same parameter to determine the starting time
  (ppr->start_sources_at_tau_c_over_tau_h) but a different parameter to determine the
  sampling frequency (ppr2->perturb_sampling_stepsize_song rather than
  ppr2->perturb_sampling_stepsize_song). */
  
  else {

    // -----------------------------------------------------------------------------
    // -                            Find first point                               -
    // -----------------------------------------------------------------------------

    double tau_ini;

    /* Using bisection, search the time such that the ratio between the Hubble
    time-scale tau_h = 1/aH and the Compton time-scale 1/kappa_dot is equal to
    ppt2->start_sources_at_tau_c_over_tau_h. Usually, this parameter is about
    0.01, which means that we start sampling the sources in the tight coupling
    regime (tau_c<<tau_h), where the visibility function is still small.  */

    if (ppt2->has_cmb == _TRUE_) {

      double tau_lower = pth->tau_ini;

      class_call (background_at_tau(pba,
                   tau_lower, 
                   pba->short_info, 
                   pba->inter_normal, 
                   &dump, 
                   pvecback),
        pba->error_message,
        ppt2->error_message);

      a = pvecback[pba->index_bg_a];
      Hc = a*pvecback[pba->index_bg_H];
  
      class_call (thermodynamics_at_z(pba,
                    pth,
                    1/a-1,
                    pth->inter_normal,
                    &dump,
                    pvecback,
                    pvecthermo),
        pth->error_message,
        ppt2->error_message);

      kappa_dot = pvecthermo[pth->index_th_dkappa];
  
      class_test (
        Hc/kappa_dot > ppr->start_sources_at_tau_c_over_tau_h,
        ppt2->error_message,
        "your choice of initial time for computing sources is inappropriate: it corresponds\
 to an earlier time than the one at which the integration of thermodynamical variables\
 started (tau=%g). You should increase either 'start_sources_at_tau_c_over_tau_h' or\
 'recfast_z_initial'\n",
        tau_lower);
     
      /* The upper limit is when the visibility function peaks */
      double tau_upper = pth->tau_rec;
    
      class_call (background_at_tau(pba,
                    tau_upper, 
                    pba->short_info, 
                    pba->inter_normal, 
                    &dump, 
                    pvecback),
        pba->error_message,
        ppt2->error_message);

      a = pvecback[pba->index_bg_a];
      Hc = a*pvecback[pba->index_bg_H];      

      class_call (thermodynamics_at_z(pba,
                    pth,
                    1/a-1,
                    pth->inter_normal,
                    &dump,
                    pvecback,
                    pvecthermo),
        pth->error_message,
        ppt2->error_message);
      
      kappa_dot = pvecthermo[pth->index_th_dkappa];

      class_test (Hc/kappa_dot < ppr->start_sources_at_tau_c_over_tau_h,
        ppt2->error_message,
        "your choice of initial time for computing sources is inappropriate: it corresponds\
 to a time after recombination. You should decrease 'start_sources_at_tau_c_over_tau_h'\n");
    
      double tau_mid = 0.5*(tau_lower + tau_upper);
  
      while (tau_upper - tau_lower > ppr->tol_tau_approx) {

        class_call (background_at_tau(pba,
                      tau_mid, 
                      pba->short_info, 
                      pba->inter_normal, 
                      &dump, 
                      pvecback),
          pba->error_message,
          ppt2->error_message);
    
        a = pvecback[pba->index_bg_a];
        Hc = a*pvecback[pba->index_bg_H];      

        class_call (thermodynamics_at_z(pba,
                      pth,
                      1/a-1,
                      pth->inter_normal,
                      &dump,
                      pvecback,
                      pvecthermo),
          pth->error_message,
          ppt2->error_message);
    
        kappa_dot = pvecthermo[pth->index_th_dkappa];
    
        if (Hc/kappa_dot > ppr->start_sources_at_tau_c_over_tau_h)
          tau_upper = tau_mid;
        else
          tau_lower = tau_mid;

        tau_mid = 0.5*(tau_lower + tau_upper);
      }

      tau_ini = tau_mid;
    }

    /* If the CMB is not requested, just start sampling the perturbations at
    recombination time */

    else {
      
      tau_ini = pth->tau_rec;

    } // end of if(has_cmb)

    int counter = 1;

    /* The next sampling point is determined by the lowest of two timescales: the time
    variation of the visibility function and the acceleration parameter. Schematically:
      
      next = previous + ppr2->perturb_sampling_stepsize_song * timescale_source
    
    where:

      timescale_source = 1 / (1/timescale_source1 + 1/timescale_source2)
      timescale_source1 = g/g_dot
      timescale_source2 = 1/sqrt |2*a_dot_dot/a - Hc^2| */

    double tau = tau_ini;

    while (tau < pba->conformal_age) {

      class_call (background_at_tau(pba,
                    tau, 
                    pba->short_info, 
                    pba->inter_normal, 
                    &dump, 
                    pvecback),
        pba->error_message,
        ppt2->error_message);

      a = pvecback[pba->index_bg_a];
      Hc = a*pvecback[pba->index_bg_H];
      H_prime = pvecback[pba->index_bg_H_prime];

      class_call (thermodynamics_at_z(pba,
                    pth,
                    1./pvecback[pba->index_bg_a]-1.,  /* redshift z=1/a-1 */
                    pth->inter_normal,
                    &dump,
                    pvecback,
                    pvecthermo),
        pth->error_message,
        ppt2->error_message);

      kappa_dot = pvecthermo[pth->index_th_dkappa];

      /* If the CMB is requested, the time sampling needs to be denser at recombination
      and when the ISW effect is important */ 

      if (ppt2->has_cmb == _TRUE_) {

        /* Variation rate of thermodynamics variables */
        double rate_thermo = pvecthermo[pth->index_th_rate];
    
        /* Variation rate of metric due to late ISW effect (important at late times) */
        double a_primeprime_over_a = a*H_prime + 2*Hc*Hc;
        double rate_isw_squared = fabs (2*a_primeprime_over_a - Hc*Hc);

        /* Compute rate */
        timescale_source = sqrt(rate_thermo*rate_thermo + rate_isw_squared);

      }
    
      /* If the CMB is not requested, we use as sampling frequency the conformal Hubble
      rate aH, which is the natural time scale of the differential system. Since 1/aH is
      propotional to the conformal time tau, this sampling roughly corresponds to a logarithmic
      sampling in tau. */
    
      else {

        /* Variation rate given by Hubble time */
        timescale_source = Hc;

      }
    
      /* Check it is non-zero */
      class_test(timescale_source == 0., ppt2->error_message,
        "null evolution rate, integration is diverging");

      /* Compute inverse rate */
      timescale_source = 1/timescale_source;

      class_test(
        fabs(ppr2->perturb_sampling_stepsize_song*timescale_source/tau)
          < ppr->smallest_allowed_variation,
        ppt2->error_message,
        "integration step =%e < machine precision: leads to infinite loop",
        ppr2->perturb_sampling_stepsize_song*timescale_source);

      tau = tau + ppr2->perturb_sampling_stepsize_song * timescale_source; 
      counter++;

    }

    /* Total number of time steps */
    ppt2->tau_size = counter;
    class_alloc (ppt2->tau_sampling, ppt2->tau_size * sizeof(double), ppt2->error_message);



    // -----------------------------------------------------------------------------
    // -                           Fill time sampling                              -
    // -----------------------------------------------------------------------------

    /* First sampling point = when the universe stops being opaque */
    counter = 0;
    ppt2->tau_sampling[counter] = tau_ini;
    tau = tau_ini;

    while (tau < pba->conformal_age) {
    
      class_call (background_at_tau(pba,
                    tau, 
                    pba->short_info, 
                    pba->inter_normal, 
                    &dump, 
                    pvecback),
        pba->error_message,
        ppt2->error_message);

      a = pvecback[pba->index_bg_a];
      Hc = a*pvecback[pba->index_bg_H];
      H_prime = pvecback[pba->index_bg_H_prime];

      class_call (thermodynamics_at_z(pba,
                    pth,
                    1/a-1,
                    pth->inter_normal,
                    &dump,
                    pvecback,
                    pvecthermo),
        pth->error_message,
        ppt2->error_message);

      kappa_dot = pvecthermo[pth->index_th_dkappa];

      if (ppt2->has_cmb == _TRUE_) {

        /* Variation rate of thermodynamics variables */
        double rate_thermo = pvecthermo[pth->index_th_rate];
    
        /* Variation rate of metric due to late ISW effect (important at late times) */
        double a_primeprime_over_a = a*H_prime + 2*Hc*Hc;
        double rate_isw_squared = fabs (2*a_primeprime_over_a - Hc*Hc);

        /* Compute rate */
        timescale_source = sqrt(rate_thermo*rate_thermo + rate_isw_squared);

      }

      else {

        /* Variation rate given by Hubble time */
        timescale_source = Hc;

      }

      /* Compute inverse rate */
      timescale_source = 1/timescale_source;

      tau = tau + ppr2->perturb_sampling_stepsize_song*timescale_source; 
      counter++;
      ppt2->tau_sampling[counter] = tau;
    }

    /* Last sampling point = exactly today */
    ppt2->tau_sampling[counter] = pba->conformal_age;

  } // end of if (has_custom_timesampling == _FALSE_)



  // ====================================================================================
  // =                                  Recombination                                   =
  // ====================================================================================

  /* Find the time when recombination ends, and store the corresponding time index
  into ppt2->index_tau_end_of_recombination */

  class_call (perturb2_end_of_recombination (
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2),
    ppt2->error_message,
    ppt2->error_message);
  

  /* If all of the requested line-of-sight sources are located at recombination or
  earlier, cut the time-sampling vector so that sources at later times are not
  computed. */

  if ((ppt2->has_recombination_only == _TRUE_) && (ppt2->has_custom_timesampling == _FALSE_)) {
    ppt2->tau_size = ppt2->index_tau_end_of_recombination;
  }

  // ====================================================================================
  // =                             Allocate sources array                               =
  // ====================================================================================

  /* Loop over types, k1, k2, and k3.  For each combination of them, allocate the tau level. */

  if(ppt2->perturbations2_verbose > 2)
    printf(" -> allocating memory for the sources array\n");
  
  /* Keep track of memory usage (debug only) */
  ppt2->count_memorised_sources = 0;
  ppt2->count_allocated_sources = 0;
  
  /* Allocate k1 level.  The further levels (k2, k3 and time) will be allocated when needed */
  for (int index_type = 0; index_type < ppt2->tp2_size; index_type++)
    class_alloc (ppt2->sources[index_type], ppt2->k_size * sizeof(double **), ppt2->error_message);
  
  /* Allocate and initialize the logical array that keeps track of the memory state of ppt2->sources */
  class_calloc (ppt2->has_allocated_sources, ppt2->k_size, sizeof(short), ppt2->error_message);



  // =====================================================================================
  // =                           Custom quadsources sampling                             =
  // =====================================================================================

  /* The user can specify a time sampling for the quadratic sources via the parameter
  file, by providing a start time, and end time and a sampling method (linear or
  logarithmic). */

  if (ppt->has_custom_timesampling_for_quadsources == _TRUE_) {

    ppt->tau_size_quadsources = ppt->custom_tau_size_quadsources;
    class_alloc (ppt->tau_sampling_quadsources, ppt->tau_size_quadsources*sizeof(double),
      ppt2->error_message);
    
    /* If the user set the custom end-time to 0, we assume that he wants to compute
    the quadratic sources all the way to today */
    if (ppt->custom_tau_end_quadsources == 0)
      ppt->custom_tau_end_quadsources = pba->conformal_age;
  
    /* Linear sampling */
    if (ppt->custom_tau_mode_quadsources == lin_tau_sampling) {
      lin_space(ppt->tau_sampling_quadsources, ppt->custom_tau_ini_quadsources,
        ppt->custom_tau_end_quadsources, ppt->tau_size_quadsources);
    }
    /* Logarithmic sampling */
    else if (ppt->custom_tau_mode_quadsources == log_tau_sampling) {
      log_space(ppt->tau_sampling_quadsources, ppt->custom_tau_ini_quadsources,
        ppt->custom_tau_end_quadsources, ppt->tau_size_quadsources);
    }
  }



  // =====================================================================================
  // =                          Standard quadsources sampling                            =
  // =====================================================================================

  /* Determine the time sampling of the quadratic sources using a simple step based
  on the Hubble rate. The quadratic sources are just first-order perturbations as a
  function of time. Unlike the line-of-sight sources, they do not involve the sharply
  changing visibility function. Therefore, we sample them using as time step the
  inverse Hubble rate, 1/Hc=1/(aH), weighted by the parameter 
  ppr2->perturb_sampling_stepsize_quadsources. */

  else {

    // -----------------------------------------------------------------------------
    // -                         Find first & last point                           -
    // -----------------------------------------------------------------------------
  
    /* The quadratic sources are needed to close the second-order system. Therefore,
    they need to be sampled from the very start of the system evolution. The system
    is evolved once for each wavemode triplet; for each triplet there is a different
    evolution start time. Here we take the smallest of these starting times,
    corresponding to the highest k, and set the quadratic sources sampling to start
    at that time. */

    double tau_ini_quadsources;

    class_call (perturb2_start_time_evolution (
                  ppr,
                  ppr2,
                  pba,
                  pth,
                  ppt,
                  ppt2,
                  ppt2->k_max,
                  &tau_ini_quadsources),
      ppt2->error_message,
      ppt2->error_message);
  
    /* Make sure that we start evolving the system before we start storing
    the perturbations. */
    tau_ini_quadsources = MIN (tau_ini_quadsources, ppt2->tau_sampling[0]);

    /* The user might have specified a custom start time for the evolution
    of the second-order system; if so, use it also to determine the
    starting time of the quadratic sources. */
    if (ppr2->custom_tau_start_evolution != 0)
      tau_ini_quadsources = ppr2->custom_tau_start_evolution;
  
    /* The last point in the time sampling of the qudratic sources has to be the
    last time we are interested in at second-order */
    double tau_end_quadsources = ppt2->tau_sampling[ppt2->tau_size-1];


    // -----------------------------------------------------------------------------
    // -                           Find sampling size                              -
    // -----------------------------------------------------------------------------

    /* In order to determine the next sampling point, we do similarly to what is done 
    in the first-order CLASS: we determine a timescale for the evolution and choose
    the next sample point at a given time based on that timescale at that time. The
    timescale we choose is 1/aH as it is the evolution timescale of the system.
    Schematically, we shall choose the next point as:
    
      next = previous + ppr->perturb_sampling_stepsize_quadsources * (1/aH)
    
    It is interesting to note that, since 1/aH is proportional to tau, this sampling
    is basically a logarithmic sampling.  The sampling becomes more dense during matter
    domination because the coefficient between aH and 1/tau is larger than the same
    coefficient during radiation domination. */

    double tau = tau_ini_quadsources;

    int counter=1;

    while (tau < tau_end_quadsources) {

      class_call (background_at_tau(
                    pba,
                    tau, 
                    pba->short_info, 
                    pba->inter_normal, 
                    &dump, 
                    pvecback),
        pba->error_message,
        ppt2->error_message);

      /* We set the variation rate as the Hubble time */
      double timescale_source = pvecback[pba->index_bg_H]*pvecback[pba->index_bg_a];

      class_test (timescale_source == 0., ppt2->error_message,
        "null evolution rate, integration is diverging");

      /* Compute inverse rate */
      timescale_source = 1/timescale_source;

      class_test(
        fabs(ppr->perturb_sampling_stepsize_quadsources*timescale_source/tau)
          < ppr->smallest_allowed_variation,
        ppt2->error_message,
        "time step =%e < machine precision: leads to infinite loop",
        ppr->perturb_sampling_stepsize_quadsources*timescale_source);

      tau = tau + ppr->perturb_sampling_stepsize_quadsources * timescale_source; 
      ++counter;
    }

    /* Total number of time steps */
    ppt->tau_size_quadsources = counter;
    class_alloc (ppt->tau_sampling_quadsources, ppt->tau_size_quadsources*sizeof(double),
      ppt2->error_message);


    // -----------------------------------------------------------------------------
    // -                           Fill time sampling                              -
    // -----------------------------------------------------------------------------
    
    counter = 0;
    ppt->tau_sampling_quadsources[counter] = tau_ini_quadsources;
    tau = tau_ini_quadsources;
  
    while (tau < tau_end_quadsources) {
    
      class_call (background_at_tau(pba,
                    tau, 
                    pba->short_info, 
                    pba->inter_normal, 
                    &dump, 
                    pvecback),
        pba->error_message,
        ppt2->error_message);
  
      /* We set the variation rate as the Hubble time */
      double timescale_source = pvecback[pba->index_bg_H]*pvecback[pba->index_bg_a];

      /* Compute inverse rate */
      timescale_source = 1/timescale_source;
  
      tau = tau + ppr->perturb_sampling_stepsize_quadsources*timescale_source; 
      ++counter;
      ppt->tau_sampling_quadsources[counter] = tau;  
    }
  
    /* The last sampling point is the last sampling point of the second-order
    sources */
    ppt->tau_sampling_quadsources[counter] = tau_end_quadsources;
  
  } // end of if(has_custom_timesampling_for_quadsources==_FALSE_)

  /* Debug - print the time sampling for the line of sight sources */
  // {
  //   int index_tau;
  //   for (index_tau=0; index_tau < ppt2->tau_size; ++index_tau)
  //     printf("%12d %17.7g\n", index_tau, ppt2->tau_sampling[index_tau]);
  // }

  /* Debug - print the time sampling for the quadratic sources */
  // {
  //   int index_tau;
  //   for (index_tau=0; index_tau < ppt->tau_size_quadsources; ++index_tau)
  //     printf("%12d %17.7g\n", index_tau, ppt->tau_sampling_quadsources[index_tau]);
  // }

  /* Check that the time range chosen to sample the 2nd-order sources is compatible
  with the range we chosen to compute the 1st-order ones */
  class_test(ppt2->tau_sampling[0] < ppt->tau_sampling_quadsources[0],
    ppt2->error_message,
    "the requested initial time for the sampling of the 2nd-order sources is too low.");

  class_test(ppt2->tau_sampling[ppt2->tau_size-1] >
      ppt->tau_sampling_quadsources[ppt->tau_size_quadsources-1],
    ppt2->error_message,
    "the requested final time for the sampling of the 2nd-order sources is too high.");



  // =====================================================================================
  // =                                   Print info                                      =
  // =====================================================================================

  /* Print info on k1-k2 sampling */
  if (ppt2->perturbations2_verbose > 0) {

    double k_min = ppt2->k[0];
    double k_max = ppt2->k[ppt2->k_size-1];
    printf(" -> sources k1 and k2 sampling: %d times in the range k=(%.4g,%.4g), k/k_eq=(%.4g,%.4g)\n",
      ppt2->k_size, k_min, k_max, k_min/pba->k_eq, k_max/pba->k_eq);   
  }

  if (ppt2->perturbations2_verbose > 1) {

    /* Print info on k3 sampling */
    int k3_max_size = ppt2->k3_size[ppt2->k_size-1][ppt2->k_size-1];
    double k3_min = ppt2->k3[ppt2->k_size-1][ppt2->k_size-1][0];
    double k3_max = ppt2->k3[ppt2->k_size-1][ppt2->k_size-1][k3_max_size-1];
    printf("     * sources k3 sampling for k1=k2=max: %d times in the range (%g,%g)\n",
      k3_max_size, k3_min, k3_max);
    printf("     * we shall solve the second-order differential system %ld (%.2g million) times\n",
      ppt2->count_k_configurations, (double)ppt2->count_k_configurations/1e6);

    
    /* Print info on time sampling */
    double a_ini, a_end, y_ini, y_end;
        
    /* Interpolate background quantities */
    int dump;

    class_call (background_at_tau(
                  pba,
                  ppt2->tau_sampling[0], 
                  pba->normal_info, 
                  pba->inter_normal, 
                  &dump, 
                  pvecback),
      pba->error_message,
      ppt2->error_message);

    a_ini = pvecback[pba->index_bg_a];
    y_ini = log10(a_ini/pba->a_eq);

    class_call (background_at_tau(
                  pba,
                  ppt2->tau_sampling[ppt2->tau_size-1], 
                  pba->normal_info, 
                  pba->inter_normal, 
                  &dump, 
                  pvecback),
      pba->error_message,
      ppt2->error_message);

    a_end = pvecback[pba->index_bg_a];
    y_end = log10(a_end/pba->a_eq);
    
    printf("     * 2nd-order line-of-sight sources time sampling:\n");
    printf("       %d times in the range tau=(%g,%g), a=(%.2e,%.2e), log10(a/a_eq)=(%.3g,%.3g)\n",
      ppt2->tau_size, ppt2->tau_sampling[0], ppt2->tau_sampling[ppt2->tau_size-1], a_ini, a_end, y_ini, y_end);
    printf("     * quadratic sources time sampling: %d times in the range tau=(%g,%g)\n",
      ppt->tau_size_quadsources, ppt->tau_sampling_quadsources[0], ppt->tau_sampling_quadsources[ppt->tau_size_quadsources-1]);
        
  }

  free (pvecback);
  free (pvecthermo);

  return _SUCCESS_;
  
}



/**
 * Determine when to start evolving the differential system for the wavemode
 * k and output the corresponding conformal time to tau_ini.
 *
 * The start time is determined using bisection with the same algorithm of 
 * CLASS. The starting time is determined using the same two criteria that
 * we used for the initial conditions, namely:
 *
 * - The considered wavemode should still be in tight coupling regime.
 * - The considered wavemode is still outside the horizon.
 *
 * The strictness of the two criteria is controlled by the following parameters:
 *
 *  - ppr2->start_small_k_at_tau_c_over_tau_h_song, which determines the start
 *    time using the ratio between the Compton interaction rate and the Hubble
 *    time.
 *    
 *  - ppr2->start_large_k_at_tau_h_over_tau_k_song, which determines the start
 *    time using the ratio between the Hubble time and the scale of the
 *    considered wavemode.
 *
 * The smaller the parameters, the earlier the starting time.
 */

int perturb2_start_time_evolution (
        struct precision * ppr,
        struct precision2 * ppr2,
        struct background * pba,
        struct thermo * pth,
        struct perturbs * ppt,
        struct perturbs2 * ppt2,
        double k,
        double * tau_ini
        )
{
  
  /* Temporary arrays used to store background and thermodynamics quantities */
  double *pvecback, *pvecthermo;
  class_alloc (pvecback, pba->bg_size*sizeof(double), ppt2->error_message);
  class_alloc (pvecthermo, pth->th_size*sizeof(double), ppt2->error_message);
  int dump;

  /* Lower limit on tau_ini is the first time in the background table */
  double tau_lower = pba->tau_table[0];

  /* Test that tau_lower is early enough to satisfy the conditions imposed
  on kappa_dot and on k/aH by the user */

  class_call (background_at_tau(pba,
                tau_lower, 
                pba->normal_info, 
                pba->inter_normal, 
                &dump, 
                pvecback),
    pba->error_message,
    ppt2->error_message);

  double a = pvecback[pba->index_bg_a];
  double Hc = a*pvecback[pba->index_bg_H];

  class_call (thermodynamics_at_z(pba,
                pth,
                1/a-1,
                pth->inter_normal,
                &dump,
                pvecback,
                pvecthermo),
    pth->error_message,
    ppt2->error_message);

  double kappa_dot = pvecthermo[pth->index_th_dkappa];

  /* Ratio of expansion rate to interaction rate. When it is much smaller than one,
  we are deep in the tight coupling regime. */
  double tau_c_over_tau_h = Hc/kappa_dot;
  
  /* Ratio of wavemode scale to expansion rate. When it is much smaller than one,
  the mode is well outside the horizon */
  double tau_h_over_tau_k = k/Hc;

  class_test (tau_c_over_tau_h > ppr2->start_small_k_at_tau_c_over_tau_h_song,
    ppt2->error_message,
    "your choice of initial time for integrating wavenumbers at 2nd-order is\
 inappropriate: it corresponds to a time before that at which the background has\
 been integrated. You should increase 'start_small_k_at_tau_c_over_tau_h_song' up\
 to at least %g, or decrease 'a_ini_over_a_today_default'\n", 
    tau_c_over_tau_h);

  class_test (tau_h_over_tau_k > ppr2->start_large_k_at_tau_h_over_tau_k_song,
    ppt2->error_message,
    "your choice of initial time for integrating wavenumbers at 2nd-order is\
 inappropriate: it corresponds to a time before that at which the background has\
 been integrated. You should increase 'start_large_k_at_tau_h_over_tau_k_song' up\
 to at least %g, or decrease 'a_ini_over_a_today_default'\n",
    ppt2->k[ppt2->k_size-1]/Hc);

  /* Upper limit on tau_ini is when we start sampling the sources */
  double tau_upper = ppt2->tau_sampling[0];
  double tau_mid = 0.5*(tau_lower + tau_upper);


  /* - Start bisection */
  
  while ((tau_upper - tau_lower)/tau_lower > ppr->tol_tau_approx) {

    /* Interpolate background quantities */
    class_call (background_at_tau(pba,
          tau_mid, 
          pba->normal_info, 
          pba->inter_normal, 
          &dump, 
          pvecback),
       pba->error_message,
       ppt2->error_message);

    a = pvecback[pba->index_bg_a];
    Hc = a*pvecback[pba->index_bg_H];

    /* Interpolate thermodynamical quantities */
    class_call (thermodynamics_at_z(pba,
                  pth,
                  1/a-1,
                  pth->inter_normal,
                  &dump,
                  pvecback,
                  pvecthermo),
      pth->error_message,
      ppt2->error_message);

    kappa_dot = pvecthermo[pth->index_th_dkappa];

    tau_h_over_tau_k = k/Hc;
    tau_c_over_tau_h = Hc/kappa_dot;

    /* Check that the two conditions are fulfilled */
    if ((tau_c_over_tau_h > ppr2->start_small_k_at_tau_c_over_tau_h_song) ||
        (tau_h_over_tau_k > ppr2->start_large_k_at_tau_h_over_tau_k_song)) {
      tau_upper = tau_mid;
    }
    else {
      tau_lower = tau_mid;
    }

    tau_mid = 0.5*(tau_lower + tau_upper);
    
    /* Some debug */
    // printf("tau_lower = %g\n", tau_lower);
    // printf("tau_upper = %g\n", tau_upper);
    // printf("tau_mid = %g\n", tau_mid);
    // printf("\n");
  
  } // end of bisection
  
  *tau_ini = tau_mid;
  
  free (pvecback);
  free (pvecthermo);

  return _SUCCESS_;

}


/**
 * Find the conformal time where recombination is effectively over, based on 
 * the parameter ppt2->recombination_max_to_end_ratio.
 * 
 * In order to match some analytical limits, or for debug purposes, it is useful
 * to constrain some physical effects to the time of recombination. For example,
 * we might want to consider the integrated Sachs-Wolfe effect only up to
 * the end recombination (early ISW effect), in order to separate it from the
 * contribution from dark energy (late ISW effect). By doing so, we obtain a 
 * better match with the analytical approximation for the squeezed intrinsic
 * bispectrum (see http://arxiv.org/abs/1109.1822, http://arxiv.org/abs/1204.5018,
 * http://arxiv.org/abs/1109.2043). 
 *
 * Note however that separating early and late-time effects might bring in some
 * unphysical effects, such as gauge dependences, especially at second order.
 *
 * We set the end of recombination as the time where the visibility function is
 * ppt2->recombination_max_to_end_ratio times smaller than its maximum value,
 * and store the corresponding index in ppt2->tau_sampling in the field
 * ppt2->index_tau_end_of_recombination.
 */
int perturb2_end_of_recombination (
             struct precision * ppr,
             struct precision2 * ppr2,
             struct background * pba,
             struct thermo * pth,
             struct perturbs * ppt,
             struct perturbs2 * ppt2
             )
{

  /* Temporary arrays used to store background and thermodynamics quantities */
  double *pvecback, *pvecthermo;
  class_alloc (pvecback, pba->bg_size*sizeof(double), ppt2->error_message);
  class_alloc (pvecthermo, pth->th_size*sizeof(double), ppt2->error_message);
  int dump;

  /* Extract from the thermodynamics module the redshift and conformal time where
  the visibility function 'g' peaks */
  double tau_rec;
  double z_max = pth->z_rec;
  class_call (background_tau_of_z (pba, z_max, &tau_rec),
    pba->error_message, ppt2->error_message);

  /* Interpolate background quantities at z_max */
  class_call (background_at_tau(
                pba,
                tau_rec, 
                pba->normal_info, 
                pba->inter_normal, 
                &dump, 
                pvecback),
    pba->error_message,
    ppt2->error_message);

  /* Interpolate thermodynamics quantities at z_max */
  class_call (thermodynamics_at_z(
                pba,
                pth,
                z_max,
                pth->inter_normal,
                &dump,
                pvecback,
                pvecthermo),
   pth->error_message,
   ppt2->error_message);

  /* Maximum of the visibility function */
  double g_max = pvecthermo[pth->index_th_g];

  /* Value of the visibility function when recombination ends */
  double g_end = g_max/ppt2->recombination_max_to_end_ratio;


  /* Now, find the time index in the sources sampling corresponding to
  the peak of recombination by looping over ppt2->tau_sampling */
  for (int index_tau=0; index_tau < ppt2->tau_size; ++index_tau) {

    /* We want to find a time after recombination, not before */
    if (ppt2->tau_sampling[index_tau] < tau_rec)
      continue;

    /* Interpolate background quantities */
    class_call (background_at_tau(
                  pba,
                  ppt2->tau_sampling[index_tau], 
                  pba->normal_info, 
                  pba->inter_normal, 
                  &dump, 
                  pvecback),
      pba->error_message,
      ppt2->error_message);

    double a = pvecback[pba->index_bg_a];

    /* Interpolate thermodynamics quantities */
    class_call (thermodynamics_at_z(
                  pba,
                  pth,
                  1./a-1.,
                  pth->inter_normal,
                  &dump,
                  pvecback,
                  pvecthermo),
      pth->error_message,
      ppt2->error_message);
  
    /* If we reached the time where the visibility function is smaller than g_end, then
    recombination is over */
    if (pvecthermo[pth->index_th_g] <= g_end) {

      ppt2->index_tau_end_of_recombination = index_tau + 1;
      break;
    }

  } // end of for (index_tau)
  
  if (ppt2->perturbations2_verbose > 1)
    printf ("     * recombination ends at tau=%g, index_tau=%d\n",
      ppt2->tau_sampling[ppt2->index_tau_end_of_recombination-1],
      ppt2->index_tau_end_of_recombination-1);
  
  free (pvecback);
  free (pvecthermo);

  /* Debug */
  // printf("z_rec = %g\n", pth->z_rec);
  // printf("tau_rec = %g\n", tau_rec);
  // printf("g_max = %g\n", g_max);
  // printf("g_end = %g\n", g_end);
 
  return _SUCCESS_; 
  
}


/**
 * Compute initial conditions for the second-order system.
 * 
 * The initial conditions in SONG are set deep in the radiation era, when the
 * considered mode (k1,k2,k3) is in the tight coupling regime and outside the
 * horizon. Please refer to sec. 5.4 of http://arxiv.org/abs/1405.2280 for more
 * details on the initial conditions (including non-Gaussianity) and for a
 * complete derivation of the initial conditions at second order.
 *
 * This function is called once for each k-triplet (k1,k2,k3) by perturb2_vector_init(),
 * which in turn is called by perturb2_solve(); it fills the ppw2->pv->y array. It is
 * assumed here that all values have been set previously to zero, only non-zero values
 * are set here.
 *
 * Important: mind which approximations are turned on when this function is called.
 * Usually, the tight-coupling approximation is turned on at early times, meaning that
 * any attempt to overwrite quantities that are not evolved if tca=on will result in a
 * segmentation fault or, even worse, in unpredictable behaviour.
 */
int perturb2_initial_conditions (
             struct precision * ppr,
             struct precision2 * ppr2,
             struct background * pba,
             struct thermo * pth,
             struct perturbs * ppt,
             struct perturbs2 * ppt2,
             double tau,
             struct perturb2_workspace * ppw2
             )
{

  /* Shortcuts */
  double * y = ppw2->pv->y;
  double k1 = ppw2->k1;
  double k2 = ppw2->k2;
    
  double k = ppw2->k;
  double k_sq = ppw2->k_sq;
  double k1_0 = ppw2->k1_m[0+1];
  double k2_0 = ppw2->k2_m[0+1]; 
  double * k1_ten_k2 = ppw2->k1_ten_k2;


  // ======================================================================================
  // =                          Interpolate needed quantities                             =
  // ======================================================================================

  /* Background quantities at tau */
  class_call (background_at_tau(
                pba,
                tau, 
                pba->long_info, 
                pba->inter_normal, 
                &(ppw2->last_index_back), 
                ppw2->pvecback),
    pba->error_message,
    ppt2->error_message);

  double a     = ppw2->pvecback[pba->index_bg_a];
  double Y     = log10(a/pba->a_eq) ;
  double a_sq   = a*a;
  double H     = ppw2->pvecback[pba->index_bg_H];
  double Hc    = a*H;
  double Hc_sq = Hc*Hc;
  double Omega_m = ppw2->pvecback[pba->index_bg_Omega_m];
  double Omega_r = ppw2->pvecback[pba->index_bg_Omega_r];


  /* Radiation density */
  double rho_g = ppw2->pvecback[pba->index_bg_rho_g];
  double rho_r = rho_g;
  double rho_ur = 0;
  if (pba->has_ur == _TRUE_) {
    rho_ur  =  ppw2->pvecback[pba->index_bg_rho_ur];
    rho_r   +=  rho_ur;
  }
  double frac_ur = rho_ur/rho_r;
  double frac_g = 1 - frac_ur;


  /* Thermodynamics quantities, needed by perturb2_quadratic_sources_at_tau() */
  class_call (thermodynamics_at_z(
                pba,
                pth,
                1./a-1.,  /* redshift z=1/a-1 */
                pth->inter_normal,
                &(ppw2->last_index_thermo),
                ppw2->pvecback,
                ppw2->pvecthermo),
    pth->error_message,
    ppt2->error_message);

  /* Newtonian gauge potential */
  double psi;

  /* Interpolate quadratic sources at tau, and store the result in ppw2->pvec_quadsources */
  if (ppt2->has_quadratic_sources == _TRUE_) {
    class_call(perturb2_quadratic_sources_at_tau(
                 ppr,
                 ppr2,
                 ppt,
                 ppt2,
                 tau,
                 interpolate_total,
                 ppw2
                 ),
      ppt2->error_message,
      ppt2->error_message);
  }

  
  
  /*sym sampling*/
  
  if ((ppt2->k3_sampling == sym_k3_sampling) ) {
  
  	class_call (perturb_song_sources_at_tau_and_k (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                k1,
                tau,
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources1),
    	ppt->error_message,
    	ppt2->error_message);
    	
    	class_call (perturb_song_sources_at_tau_and_k (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                k2,
                tau,
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources2),
    	ppt->error_message,
    	ppt2->error_message);
  
  }
  
  else {
  	/* Interpolate first-order quantities (ppw2->psources_1) */
  	class_call (perturb_song_sources_at_tau (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                ppw2->index_k1,
                tau,
                ppt->qs_size[ppt->index_md_scalars],
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources1),
    	ppt->error_message,
    	ppt2->error_message);
				
 		/* Interpolate first-order quantities (ppw2->psources_2) */
  	class_call (perturb_song_sources_at_tau (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                ppw2->index_k2,
                tau,
                ppt->qs_size[ppt->index_md_scalars],
                ppt->inter_normal,
                &(ppw2->last_index_sources),                 
                ppw2->pvec_sources2),
    	ppt->error_message,
    	ppt2->error_message);  

	}
  /* Shortcuts to access the first-order quantities */
  double * pvec_sources1 = ppw2->pvec_sources1;
  double * pvec_sources2 = ppw2->pvec_sources2;


  /* - Densities and velocities at first order */

  double delta_g=0, pressure_g=0, v_0_g=0, shear_0_g=0, delta_g_adiab=0;
  double delta_g_1=0, delta_g_2=0, vpot_g_1=0, vpot_g_2=0;
  double delta_b=0, pressure_b=0, v_0_b=0, shear_0_b=0;
  double delta_b_1=0, delta_b_2=0, vpot_b_1=0, vpot_b_2=0;  
  double delta_cdm=0, pressure_cdm=0, v_0_cdm=0, shear_0_cdm=0;  
  double delta_cdm_1=0, delta_cdm_2=0, vpot_cdm_1=0, vpot_cdm_2=0;
  double delta_ur=0, pressure_ur=0, v_0_ur=0, shear_0_ur=0;  
  double delta_ur_1=0, delta_ur_2=0, vpot_ur_1=0, vpot_ur_2=0;
  double phi_1, phi_2, psi_1, psi_2;

  /* Photons */
  delta_g_1 = pvec_sources1[ppt->index_qs_delta_g];
  delta_g_2 = pvec_sources2[ppt->index_qs_delta_g];
  vpot_g_1 = pvec_sources1[ppt->index_qs_v_g];
  vpot_g_2 = pvec_sources2[ppt->index_qs_v_g];

  /* Baryons */
  delta_b_1 = pvec_sources1[ppt->index_qs_delta_b];
  delta_b_2 = pvec_sources2[ppt->index_qs_delta_b];
  vpot_b_1 = pvec_sources1[ppt->index_qs_v_b];
  vpot_b_2 = pvec_sources2[ppt->index_qs_v_b];

  /* Cold dark matter */
  if (pba->has_cdm == _TRUE_) {

    delta_cdm_1 = pvec_sources1[ppt->index_qs_delta_cdm];
    delta_cdm_2 = pvec_sources2[ppt->index_qs_delta_cdm];  

    if (ppt->gauge != synchronous) {
      vpot_cdm_1 = pvec_sources1[ppt->index_qs_v_cdm];
      vpot_cdm_2 = pvec_sources2[ppt->index_qs_v_cdm];
    }
  }

  /* Neutrinos */
  if (pba->has_ur == _TRUE_) {

    delta_ur_1 = pvec_sources1[ppt->index_qs_delta_ur];
    delta_ur_2 = pvec_sources2[ppt->index_qs_delta_ur];
    vpot_ur_1 = pvec_sources1[ppt->index_qs_v_ur];
    vpot_ur_2 = pvec_sources2[ppt->index_qs_v_ur];
  }
  
  /* Newtonian gauge */
  if (ppt->gauge == newtonian) {

    phi_1 = ppw2->pvec_sources1[ppt->index_qs_phi];
    phi_2 = ppw2->pvec_sources2[ppt->index_qs_phi];
          
    psi_1 = ppw2->pvec_sources1[ppt->index_qs_psi];
    psi_2 = ppw2->pvec_sources2[ppt->index_qs_psi];
  }


  // =======================================================================================
  // =                            Vanishing initial conditions                             =
  // =======================================================================================


  /* If the user asked for vanishing initial conditions, then do not do anything, as the
    ppw2->pv->y array was initialized with calloc. Works fine in synchronous gauge. */

  if (ppt2->has_zero_ic == _TRUE_) {

    if (ppt2->perturbations2_verbose > 3)
      printf("     * assuming vanishing initial conditions for the 2nd-order system\n");

  }



  // =======================================================================================
  // =                            First-order initial conditions                           =
  // =======================================================================================

  /* These initial conditions should be used only for testing purposes.  For example,
  if you set 'quadratic_sources = no' in the .ini file and use these IC, then
  you should get the first-order tranfer functions as in standard CLASS.  Note that
  you should compare with the latter computed for k = sqrt(k1^2 + k2^2 + 2*k1_dot_k2). */

  else if (ppt2->has_ad_first_order == _TRUE_) {

    /* If we adopt first-order like IC, we need to give some initial non-Gaussianity otherwise
    the bispectrum will vanish.  We give initial the IC needed to have fnl_phi = 1, which means
    fnl_R = -3/5 where R is the comoving curvature perturbation. Note that, at second order,
    this R is different from the R (Malik & Wands) we use in the initial conditions for the
    second order system. The latter R satisfies the relation fnl_R = -1 - 3/5*fnl_phi instead
    of just fnl_R = -3/5*fnl_phi (the eternal problem of using the exponential or not...) */    
    double primordial_local_fnl_zeta = 3/5. * ppt2->primordial_local_fnl_phi;
    double primordial_local_fnl_R = - primordial_local_fnl_zeta;
    double R = 2 * primordial_local_fnl_R;    

    /* We express the initial conditions in terms of C=1/2*R, as in Ma & Bertschinger 1995. */
    double C = 0.5*R;
    
    if (ppt2->perturbations2_verbose > 3)
      printf("     * Using 'has_ad_first_order' initial conditions with C = %g... \n", C);
    

    /* Scalar initial conditions */
    if (ppr2->compute_m[0] == _TRUE_) {

      double psi = 20.*C / (15. + 4.*frac_ur);
      y[ppw2->pv->index_pt2_phi] = (1. + 2./5.*frac_ur)*psi;
           
      /* Photon density */        
      y[ppw2->pv->index_pt2_monopole_g] = -2.*psi;
    
      /* Photon velocity */
      double theta_g =  0.5 * k_sq* tau *psi;
      y[ppw2->pv->index_pt2_monopole_g + lm(1,0)] = 4*theta_g/k;   //  I_1_0 = 3 theta (w+1)/k
    
      /* Baryon density */        
      y[ppw2->pv->index_pt2_monopole_b] = 3./4.*y[ppw2->pv->index_pt2_monopole_g];
        
      /* Baryon velocity */
      y[ppw2->pv->index_pt2_monopole_b + nlm(1,1,0)] = y[ppw2->pv->index_pt2_monopole_g + lm(1,0)]/4;
    
    
      if (pba->has_cdm == _TRUE_) {
        /* Cold dark matter density */
        y[ppw2->pv->index_pt2_monopole_cdm + nlm(0,0,0)] = 3./4.*y[ppw2->pv->index_pt2_monopole_g];
    
        /* Cold dark matter velocity */
        y[ppw2->pv->index_pt2_monopole_cdm + nlm(1,1,0)] = y[ppw2->pv->index_pt2_monopole_g + lm(1,0)]/4;
      }
    

      if (pba->has_ur == _TRUE_) {

        /* Neutrino density */        
        y[ppw2->pv->index_pt2_monopole_ur] = y[ppw2->pv->index_pt2_monopole_g];
    
        /* Neutrino velocity */
        y[ppw2->pv->index_pt2_monopole_ur + lm(1,0)] = y[ppw2->pv->index_pt2_monopole_g + lm(1,0)];
    
        /* Neutrino shear (gauge invariant) */
        double shear_ur = 1/15. * k*k*tau*tau * psi;
        y[ppw2->pv->index_pt2_monopole_ur + lm(2,0)] = shear_ur/10.;      // I_2_0 = 15/2 (w+1) shear
              
      }
    }

  }



  // =======================================================================================
  // =                            Unphysical initial conditions                            =
  // =======================================================================================

  /* These initial conditions are used for testing purposes only. */

  else if (ppt2->has_unphysical_ic == _TRUE_) {

    if (ppt2->perturbations2_verbose > 3)
      printf("     * Using unphysical initial conditions...\n");
    
    y[ppw2->pv->index_pt2_monopole_ur + lm(2,0)] = 1;

    
  }



  // =======================================================================================
  // =                            Adiabatic initial conditions                             =
  // =======================================================================================

  /* Default initial conditions in SONG. Their derivation is explained in detail in section
  5.4 of my PhD thesis (http://arxiv.org/abs/1405.2280). Here we assume that there are no
  primordial vector or tensor modes from inflation or any other primordial phase. */

  else if (ppt2->has_ad == _TRUE_) {  

    if (ppt2->perturbations2_verbose > 3)
      printf("     * Using adiabatic initial conditions.\n");

    if (ppr2->compute_m[0] == _TRUE_) {

      // -----------------------------------------------------------------------
      // -                          Metric & Quadrupoles                       -
      // -----------------------------------------------------------------------
    
      /* The user can specify the amount of non-linearities in the initial condition
      via the zeta function, the gauge-invariant curvature perturbation used by Maldacena
      2003; see sec. 5.4.2 of http://arxiv.org/abs/1405.2280 for more detail.
      For the time being, SONG only allows to specify non-gaussianity of the local
      type, where zeta is equal to 2*fnl_zeta*zeta(k1)*zeta(k2). To use an arbitrary type
      of non-gaussianity, one could include an arbitrary shape function S(k1,k2,k3) for zeta
      at this point. Note that the bispectrum and Fisher modules in SONG output results
      with respect to fnl_phi, the non-linearity in the curvature perturbation phi. The two
      are related by a factor 3/5, which we include here. */
      double primordial_local_fnl_zeta = 3/5. * ppt2->primordial_local_fnl_phi;
      double zeta = 2 * primordial_local_fnl_zeta;

      /* Quadratic part of the anisotropic stresses equation, as in eq. 5.71
      of http://arxiv.org/abs/1405.2280: phi - psi = - quadrupoles + A_quad,  */
      double A_quad = ppw2->pvec_quadsources[ppw2->index_qs2_psi];
    
      /* To compute the initial conditions for the time potential psi, we need the photon
      quadrupole.  This is obtained directly from Boltzmann equation assuming an infinitely
      large interaction rate (TCA0 approximation), as in eq. 5.57 of
      http://arxiv.org/abs/1405.2280, or in eq. C.6 of Pitrou et al 2010.
      The equation can also be written as I_2_0(TCA0) = -10 (vv)[m] which in SONG reads
      20*k1_ten_k2[2]*vpot_g_1*vpot_g_2; the sign flip comes from the fact that in SONG we
      use u=iv. The form with vv makes it clear that TCA0 implies vanishing
      shear during tight coupling, as shear and quadrupole for the photons are related by
      I_2_m = -15/2 Sigma[m] - 10 (vv)[m] (eq. 4.44 of http://arxiv.org/abs/1405.2280)*/
      double I_2_0_quad = 5/8. * (c_minus_12(2,0) * I_1_tilde(1) * I_2_tilde(1)
                                + c_minus_21(2,0) * I_2_tilde(1) * I_1_tilde(1));

      /* The IC for the neutrino quadrupole come from integrating the dipole & quadrupole
      equations with H >> anything.  We store the quadratic part of N_2_0 here, as in eq.
      5.65 of http://arxiv.org/abs/1405.2280. */
      double N_2_0_quad = 0;

      if (pba->has_ur == _TRUE_)
        N_2_0_quad = 1/3.*(k/Hc_sq)*dN_qs2(1,0) + 1/(2*Hc)*dN_qs2(2,0)
          + 8/3.*(k_sq/Hc_sq)*psi_1*psi_2;

      /* Here we put together the quadratic parts of the radiation quadrupoles */
      double quadrupole_quad = frac_g * I_2_0_quad;

      if (pba->has_ur == _TRUE_)
        quadrupole_quad += frac_ur * N_2_0_quad;

      /* We multiply the quadrupoles by the same factor they are multiplied with in the
      anisotropic stresses equation. */
      quadrupole_quad *= 3/5.*(Hc_sq/k_sq);
  
      /* B_quad (eq. 5.76 of http://arxiv.org/abs/1405.2280) is needed to determine psi:
      psi * (1 + 2/5*frac_ur) = phi + B_quad */
      double B_quad = A_quad - quadrupole_quad;
    
      /* The time potential psi is obtained using B_quad, as in eq. 5.75 of
      http://arxiv.org/abs/1405.2280 */
      double frac_g_factor = 1 + 4/15.*(1-frac_g);
      psi = 1./frac_g_factor * 2/3. * (- zeta + psi_1*psi_2 - 2*phi_1*phi_2 + B_quad);

      /* For standard models of inflation, the photon quadrupole does not have a
      purely second-order part at early times (see eq. 5.57 of http://arxiv.org/abs/1405.2280
      or C.6 of P2010) */
      double I_2_0 = I_2_0_quad;

      /* Now that we obtained psi, we can compute the purely second-order part of the neutrinos
      shear, and add it to the quadratic part, which we already computed (eq. 5.64 of
      http://arxiv.org/abs/1405.2280)*/
      double N_2_0 = 0;

      if (pba->has_ur == _TRUE_)
        N_2_0 = 2/3.*(k_sq/Hc_sq)*psi + N_2_0_quad;

      /* We obtain the initiali conditions of phi from psi and B_quad, as in eq. 5.77
      of http://arxiv.org/abs/1405.2280. */
      double rho_quadrupole = rho_g*I_2_0;
      
      if (pba->has_ur == _TRUE_)
        rho_quadrupole += rho_ur*N_2_0;

      double phi = (1+2/5.*frac_ur)*psi - B_quad;

      /* Uncomment to use an almost equivalent form, in a fashion similar to what is done
      in Pitrou et al. 2010. */
      // phi = psi - A_quad + 3/5. * (a_sq/k_sq) * rho_quadrupole;


      // -----------------------------------------------------------------------
      // -                          Velocities & Dipoles                       -
      // -----------------------------------------------------------------------

      /* Similary to the first-order case, at second-order the velocities of the
      different matter species are equal at early times.  Such unique velocity
      can be computed from the longitudinal Einstein equation (G_0i = T_0i) once
      the time potential psi is known, and assuming that the potentials are constant
      at early times. */
    
      /* Quadratic terms of the longitudinal equation */
      double L_quad = ppw2->pvec_quadsources[ppw2->index_qs2_phi_prime_longitudinal];

      /* Common matter & radiation velocity (m=0 case, which is the one appearing in
      the RHS of the longitudinal equation).  Note that here we use the fact that also
      at first order v_b = v_g = v_cdm = v_ur. */
      double v_0_adiabatic = 2*(k/Hc)*(psi - L_quad/Hc)
                           - (-k1_0*vpot_cdm_1)*(3*Omega_m*delta_cdm_2 + 4*Omega_r*delta_g_2)
                           - (-k2_0*vpot_cdm_2)*(3*Omega_m*delta_cdm_1 + 4*Omega_r*delta_g_1);

      v_0_adiabatic *= 1/(3*Omega_m + 4*Omega_r);


      /* - Obtain the dipoles of the various species from the common velocity */
      
      double I_1_0=0, N_1_0=0, b_1_1_0=0, cdm_1_1_0=0;

      /* Photon dipole */
      I_1_0 = 4*(v_0_adiabatic + delta_g_1*(-k2_0*vpot_g_2) + delta_g_2*(-k1_0*vpot_g_1));
      
      /* Neutrino dipole */
      if (pba->has_ur == _TRUE_) {

        N_1_0 = 4*(v_0_adiabatic + delta_ur_1*(-k2_0*vpot_ur_2) + delta_ur_2*(-k1_0*vpot_ur_1));

        /* One can also compute the neutrino dipole by directly using its dipole
        equation, assuming that psi and dN_qs2(1,0) are constant at early times. The two
        ways of computing N(1,0) give the same result in the limit where the initial
        conditions are set at tau=0. Here we check that the two ways of computing N_1_0
        give the same answer to the 10% level. Note that when N_1_0 is very small, this
        check is likely to fail but we don't care much about that because... N_1_0 is
        very small. */
        double N_1_0_boltzmann = 2*(k/Hc)*psi + 8*(k/Hc)*psi_1*psi_2 + 1/Hc*dN_qs2(1,0);

        class_test_nothing (fabs(1-N_1_0/N_1_0_boltzmann) > 0.1,
          ppt2->error_message,
          "consistency check failed (%.5g!=%.5g), diff=%g try evolving from earlier on",
          N_1_0, N_1_0_boltzmann, fabs(1-N_1_0/N_1_0_boltzmann));

      } // end of if(has_ur)

      /* Baryons */
      b_1_1_0 = 3*(v_0_adiabatic + delta_b_1*(-k2_0*vpot_b_2) + delta_b_2*(-k1_0*vpot_b_1));

      /* Cold dark matter */
      if (pba->has_cdm == _TRUE_)
        cdm_1_1_0 = 3*(v_0_adiabatic + delta_cdm_1*(-k2_0*vpot_cdm_2) + delta_cdm_2*(-k1_0*vpot_cdm_1));
    
      /* Uncomment to set the primordial velocity to zero */
      // I_1_0 = N_1_0 = b_1_1_0 = cdm_1_1_0 = 0;


      // -----------------------------------------------------------------------
      // -                              Monopoles                              -
      // -----------------------------------------------------------------------

      /* Photon monopole (like in eq. 3.5b of P2010) */
      double I_0_0 = - 2*psi + 8*psi_1*psi_2;
      
      /* Neutrino monopole */
      double N_0_0 = 0;
      if (pba->has_ur == _TRUE_)
        N_0_0 = I_0_0;

      /* Baryon monopole (eq. 3.4b of P2010) */
      double b_0_0_0 = 3*(I_0_0/4 - delta_g_1*delta_g_2/16);

      /* CDM monopole */
      double cdm_0_0_0 = b_0_0_0;


      // -----------------------------------------------------------------------
      // -                               Update y                              -
      // -----------------------------------------------------------------------      

      /* Metric */
      y[ppw2->pv->index_pt2_phi] = phi;

      /* Photons */
      y[ppw2->pv->index_pt2_monopole_g] = I_0_0;
      if (ppw2->approx[ppw2->index_ap2_tca] == (int)tca_off) {
        y[ppw2->pv->index_pt2_monopole_g + lm(1,0)] = I_1_0;
        y[ppw2->pv->index_pt2_monopole_g + lm(2,0)] = I_2_0;
      }

      /* Baryons */
      y[ppw2->pv->index_pt2_monopole_b] = b_0_0_0;
      y[ppw2->pv->index_pt2_monopole_b + nlm(1,1,0)] = b_1_1_0;

      /* Cold dark matter */
      if (pba->has_cdm == _TRUE_) {
        y[ppw2->pv->index_pt2_monopole_cdm] = cdm_0_0_0;
        if (ppt->gauge != synchronous)
          y[ppw2->pv->index_pt2_monopole_cdm + nlm(1,1,0)] = cdm_1_1_0;
      }

      /* Neutrinos */
      if (pba->has_ur == _TRUE_) {
        y[ppw2->pv->index_pt2_monopole_ur] = N_0_0;
        y[ppw2->pv->index_pt2_monopole_ur + lm(1,0)] = N_1_0;
        y[ppw2->pv->index_pt2_monopole_ur + lm(2,0)] = N_2_0;
      }           
    
    } // end of scalar modes


    /* TODO: include initial conditions for m!=0. These are given by the scalar quadratic
    sources only. Make sure to express them in terms of rot_1 and rot_2 because in this
    way you automatically account for the sin(theta_1) rescaling */
        
  } // end of if(has_ad)  

  return _SUCCESS_;

}







int perturb2_workspace_init (
         struct precision * ppr,
         struct precision2 * ppr2,
         struct background * pba,
         struct thermo * pth,
         struct perturbs * ppt,
         struct perturbs2 * ppt2,
         struct perturb2_workspace * ppw2
         )
{

  /* Maximum value for the angular scale */
  ppw2->l_max_g = ppr2->l_max_g;
  ppw2->l_max_pol_g = ppr2->l_max_pol_g;
  ppw2->l_max_ur = ppr2->l_max_ur;

  
  // ============================================================================================
  // =                                   Count the metric quantities                            =
  // ============================================================================================

  /* Define indices of metric perturbations obeying to constraint equations.  This
  can be done once and for all, because the vector of metric perturbations is
  the same whatever the approximation scheme, unlike the vector of quantities to
  be integrated, which is allocated separately in perturb2_vector_init. */
  int index_mt = 0;


  /* Newtonian gauge */
  if (ppt->gauge == newtonian) {

    /* We are going to evolve the curvature potential phi, and use the anisotropic stresses
    constraint equation to obtain psi. We obtain phi_prime from either the time-time or
    longitudinal Einstein equations. */

    /* Scalar potentials */
    if (ppr2->compute_m[0] == _TRUE_) {
      ppw2->index_mt2_psi = index_mt++;                         /* Psi */
      ppw2->index_mt2_phi_prime = index_mt++;                   /* Phi' */
      ppw2->index_mt2_phi_prime_poisson = index_mt++;           /* Phi' from the Poisson equation */
      ppw2->index_mt2_phi_prime_longitudinal = index_mt++;      /* Phi' from the longitudinal equation */
    }

    /* Vector potentials */    
    if (ppr2->compute_m[1] == _TRUE_)
      ppw2->index_mt2_omega_m1_prime = index_mt++;            /* Omega_[m=1] from the vector part of g^i_0 */

    /* Tensor potentials */
    if (ppr2->compute_m[2] == _TRUE_)
      ppw2->index_mt2_gamma_m2_prime_prime = index_mt++;      /* Gamma_[m=2] from the tensor part of g^i_j */

  }

    
  /* Synchronous gauge */
  if (ppt->gauge == synchronous) {
 
    /* We do not include eta because it is evolved, while here we only consider
    quantities obeying to constraint equations) */

    /* Scalar potentials */
    if (ppr2->compute_m[0] == _TRUE_) {
      ppw2->index_mt2_h_prime = index_mt++;                     /* h' */
      ppw2->index_mt2_h_prime_prime = index_mt++;               /* h'' */
      ppw2->index_mt2_eta_prime = index_mt++;                   /* eta' */
      ppw2->index_mt2_alpha_prime = index_mt++;                 /* alpha' (with alpha = (h' + 6 eta') / (2 k**2) ) */
    }
  }     

  /* Set the size of the array of metric quantities */
  ppw2->mt2_size = index_mt;



  // ===========================================================================================
  // =                                     Count the approximations                            =
  // ===========================================================================================

  /* Count number of approximation, initialize their indices, and allocate their flags */
  int index_ap = 0;

  ppw2->index_ap2_tca = index_ap++;
  ppw2->index_ap2_rsa = index_ap++;
  if (pba->has_ur)
    ppw2->index_ap2_ufa = index_ap++;
  ppw2->index_ap2_nra = index_ap++;

  ppw2->ap2_size = index_ap;

  if (ppw2->ap2_size > 0)
    class_alloc (ppw2->approx, ppw2->ap2_size*sizeof(int), ppt2->error_message);

  /* Assign values to the approximation for definitness. These will be overwritten
  in perturb2_approximations. The TCA approximation must be turned on at tau_ini,
  see comment to perturb2_find_approximation_number(). */
  ppw2->approx[ppw2->index_ap2_tca] = (int)tca_on;
  ppw2->approx[ppw2->index_ap2_rsa] = (int)rsa_off;
  if (pba->has_ur)
    ppw2->approx[ppw2->index_ap2_ufa] = (int)ufa_off;
  ppw2->approx[ppw2->index_ap2_nra] = (int)nra_off;


  // ==========================================================================================
  // =                                      Allocate arrays                                   =
  // ==========================================================================================

  /* Allocate the arrays in which we will store temporarily the values of background,
  thermodynamics, metric and source quantities at a given time.  The pvec_sources1
  and pvec_sources2 arrays will contain the first order sources, while
  pvec_quadsources  will contain the quadratic terms in the Einstein equations,
  Liouville operator and collision term. */
        
  class_alloc (ppw2->pvecback, pba->bg_size*sizeof(double), ppt2->error_message);
  class_alloc (ppw2->pvecthermo, pth->th_size*sizeof(double), ppt2->error_message);
  class_alloc (ppw2->pvecmetric, ppw2->mt2_size*sizeof(double), ppt2->error_message);

  class_calloc (ppw2->pvec_sources1, ppt->qs_size[ppt->index_md_scalars], sizeof(double), ppt2->error_message);
  class_calloc (ppw2->pvec_sources2, ppt->qs_size[ppt->index_md_scalars], sizeof(double), ppt2->error_message);


  // =========================================================================================
  // =                               Initialize quadratic sources                            =
  // =========================================================================================

  class_call (perturb2_workspace_init_quadratic_sources(
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2,
                ppw2
                ),
    ppt2->error_message,
    ppt2->error_message);


  return _SUCCESS_;

}



int perturb2_workspace_init_quadratic_sources (
         struct precision * ppr,
         struct precision2 * ppr2,
         struct background * pba,
         struct thermo * pth,
         struct perturbs * ppt,
         struct perturbs2 * ppt2,
         struct perturb2_workspace * ppw2
         )
{

  // =====================================================================================
  // =                            Count metric quadratic sources                         =
  // =====================================================================================

  int index_qs2 = 0;

  // ---------------------------------------------------------
  // -                     Newtonian gauge                   -
  // ---------------------------------------------------------
  
  if (ppt->gauge == newtonian) {

    /* Scalar potentials */
    if (ppr2->compute_m[0] == _TRUE_) {
      ppw2->index_qs2_psi = index_qs2++;                         /* Psi */
      if (ppt2->has_isw == _TRUE_)  
        ppw2->index_qs2_psi_prime = index_qs2++;                 /* Psi' */
      ppw2->index_qs2_phi_prime = index_qs2++;                   /* Phi' */
      ppw2->index_qs2_phi_prime_poisson = index_qs2++;           /* Phi' from the Poisson equation */
      ppw2->index_qs2_phi_prime_longitudinal = index_qs2++;      /* Phi' from the longitudinal equation */
    }

    /* Scalar potentials */
    if (ppr2->compute_m[1] == _TRUE_)
      ppw2->index_qs2_omega_m1_prime = index_qs2++;            /* Omega_[m=1] from the vector part of g^i_0 */

    /* Tensor potentials */
    if (ppr2->compute_m[2] == _TRUE_)
      ppw2->index_qs2_gamma_m2_prime_prime = index_qs2++;      /* Gamma_[m=2] from the tensor part of g^i_j */

  }

    
  // ---------------------------------------------------------
  // -                    Synchronous gauge                  -
  // ---------------------------------------------------------
  
  if (ppt->gauge == synchronous) {
 
    /* Scalar potentials */
    if (ppr2->compute_m[0] == _TRUE_) {
      ppw2->index_qs2_h_prime = index_qs2++;                     /* h' */
      ppw2->index_qs2_h_prime_prime = index_qs2++;               /* h'' */
      ppw2->index_qs2_eta_prime = index_qs2++;                   /* eta' */
      ppw2->index_qs2_alpha_prime = index_qs2++;                 /* alpha' (with alpha = (h' + 6 eta') / (2 k**2) ) */
    }

  }     

  // ====================================================================================================
  // =                           Count Liouville & collisional quadratic sources                        =
  // ====================================================================================================

  // ---------------------------------------
  // -          Photon temperature         -
  // ---------------------------------------

  /* Number of equations in the photon temperature hierarchy */
  ppw2->n_hierarchy_g = size_l_indexm (ppw2->l_max_g, ppt2->m, ppt2->m_size);

  /* The first moment of the hierarchy is the monopole l=0, m=0 */
  ppw2->index_qs2_monopole_g = index_qs2;
  index_qs2 += ppw2->n_hierarchy_g;


  // ----------------------------------------
  // -          Photon polarization         -
  // ----------------------------------------

  if (ppt2->has_polarization2 == _TRUE_) {

    ppw2->n_hierarchy_pol_g = size_l_indexm (ppw2->l_max_pol_g, ppt2->m, ppt2->m_size);

    /* Photon E-mode polarization */
    ppw2->index_qs2_monopole_E = index_qs2;
    index_qs2 += ppw2->n_hierarchy_pol_g;

    /* Photon B-mode polarization */
    ppw2->index_qs2_monopole_B = index_qs2;
    index_qs2 += ppw2->n_hierarchy_pol_g;
    
  } // end of if(has_polarization2)


  // -------------------------------------------------------
  // -              Ultra Relativistic Neutrinos           -
  // -------------------------------------------------------
  if (pba->has_ur == _TRUE_) {

    ppw2->n_hierarchy_ur = size_l_indexm (ppw2->l_max_ur, ppt2->m, ppt2->m_size);

    ppw2->index_qs2_monopole_ur = index_qs2;
    index_qs2 += ppw2->n_hierarchy_ur;
  
  }

  // ---------------------------------------
  // -                Baryons              -
  // ---------------------------------------

  /* In a perfect fluid we only need to evolve the n<=1 beta-moments. */
  if (ppt2->has_perfect_baryons == _FALSE_)
    ppw2->n_hierarchy_b = size_n_l_indexm (2, 2, ppt2->m, ppt2->m_size);
  else
    ppw2->n_hierarchy_b = size_n_l_indexm (1, 1, ppt2->m, ppt2->m_size);

  ppw2->index_qs2_monopole_b = index_qs2;
  index_qs2 += ppw2->n_hierarchy_b;    

  /* When evolving a perfect fluid, we set the beta-moments with n=2 to be proportional to v*v */
  if (ppt2->has_perfect_baryons == _TRUE_) {
    ppw2->index_qs2_dd_b = index_qs2++;
    ppw2->index_qs2_vv_b = index_qs2++;
  }


  // ----------------------------------------------
  // -               Cold Dark Matter             -
  // ----------------------------------------------
  if (pba->has_cdm == _TRUE_) {

    /* In a perfect fluid we only need to evolve the n<=1 beta-moments. */
    if (ppt2->has_perfect_cdm == _FALSE_)
      ppw2->n_hierarchy_cdm = size_n_l_indexm (2, 2, ppt2->m, ppt2->m_size);
    else
      ppw2->n_hierarchy_cdm = size_n_l_indexm (1, 1, ppt2->m, ppt2->m_size);
    
    ppw2->index_qs2_monopole_cdm = index_qs2;
    index_qs2 += ppw2->n_hierarchy_cdm;

    /* When evolving a perfect fluid, we set the beta-moments with n=2 to be proportional to v*v */
    if (ppt2->has_perfect_cdm == _TRUE_)
      ppw2->index_qs2_vv_cdm = index_qs2++;
    
  }
  
  // ----------------------------------------------
  // -               magnetic fiels		            -
  // ----------------------------------------------
  
  if (ppt2->has_magnetic_field == _TRUE_) {
  	ppw2->index_qs2_monopole_mag = index_qs2;
  	index_qs2 += size_l_indexm (1, ppt2->m, ppt2->m_size);
  }
  
  
  /* Set the size of the quadratic sources */
  ppw2->qs2_size = index_qs2;

	


  // ==========================================================================================
  // =                                       Allocate arrays                                  =
  // ==========================================================================================
        
  // ---------------------------------------------------------
  // -                    Quadratic sources                  -
  // ---------------------------------------------------------
  
  /* Allocate the tables that will contain the quadratic sources for all types and for all the times
  in ppt->tau_sampling_quadsources.  They all have two levels, for example:
  ppw2->quadsources_table[index_qs2_type][index_tau]. */
  class_calloc (ppw2->quadsources_table, ppw2->qs2_size, sizeof(double), ppt2->error_message);  
  for (index_qs2=0; index_qs2<ppw2->qs2_size; ++index_qs2)
    class_calloc (ppw2->quadsources_table[index_qs2], ppt->tau_size_quadsources, sizeof(double), ppt2->error_message);

  class_calloc (ppw2->quadcollision_table, ppw2->qs2_size, sizeof(double), ppt2->error_message);  
  for (index_qs2=0; index_qs2<ppw2->qs2_size; ++index_qs2)
    class_calloc (ppw2->quadcollision_table[index_qs2], ppt->tau_size_quadsources, sizeof(double), ppt2->error_message);

  /* Allocate the arrays that will contain the second-derivative of the table arrays, in view of
  spline interpolation */
  class_calloc (ppw2->dd_quadsources_table, ppw2->qs2_size, sizeof(double), ppt2->error_message);  
  for (index_qs2=0; index_qs2<ppw2->qs2_size; ++index_qs2)
    class_calloc (ppw2->dd_quadsources_table[index_qs2], ppt->tau_size_quadsources, sizeof(double), ppt2->error_message);

  class_calloc (ppw2->dd_quadcollision_table, ppw2->qs2_size, sizeof(double), ppt2->error_message);  
  for (index_qs2=0; index_qs2<ppw2->qs2_size; ++index_qs2)
    class_calloc (ppw2->dd_quadcollision_table[index_qs2], ppt->tau_size_quadsources, sizeof(double), ppt2->error_message);

  /* Allocate the temporary arrays that will contain the interpolated values of the quadratic sources
  contained in the above tables, at a certain time */
  class_calloc (ppw2->pvec_quadsources, ppw2->qs2_size, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->pvec_quadcollision, ppw2->qs2_size, sizeof(double), ppt2->error_message);


  // -----------------------------------------------------------
  // -                   Rotated multipoles                    -
  // -----------------------------------------------------------

  int n_rotation_coefficients = size_l_m (ppt2->largest_l_quad, ppt2->largest_l_quad);

  /* Allocate memory for the rotation coefficients arrays */
  class_calloc (ppw2->rotation_1, n_rotation_coefficients, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->rotation_2, n_rotation_coefficients, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->rotation_1_minus, n_rotation_coefficients, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->rotation_2_minus, n_rotation_coefficients, sizeof(double), ppt2->error_message);


  /* Allocate memory for the inner products */

  int n_multipoles = size_l_indexm (ppt2->largest_l, ppt2->m, ppt2->m_size);

  class_calloc (ppw2->c_minus_product_12, n_multipoles, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->c_minus_product_21, n_multipoles, sizeof(double), ppt2->error_message);    
  class_calloc (ppw2->c_plus_product_12, n_multipoles, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->c_plus_product_21, n_multipoles, sizeof(double), ppt2->error_message);   

  class_calloc (ppw2->c_minus_product_11, n_multipoles, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->c_minus_product_22, n_multipoles, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->c_plus_product_11, n_multipoles, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->c_plus_product_22, n_multipoles, sizeof(double), ppt2->error_message);

  class_calloc (ppw2->r_minus_product_12, n_multipoles, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->r_minus_product_21, n_multipoles, sizeof(double), ppt2->error_message);    
  class_calloc (ppw2->r_plus_product_12, n_multipoles, sizeof(double), ppt2->error_message);
  class_calloc (ppw2->r_plus_product_21, n_multipoles, sizeof(double), ppt2->error_message);   

  if (ppt2->has_polarization2) {

    class_calloc (ppw2->d_minus_product_12, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->d_minus_product_21, n_multipoles, sizeof(double), ppt2->error_message);    
    class_calloc (ppw2->d_plus_product_12, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->d_plus_product_21, n_multipoles, sizeof(double), ppt2->error_message);   

    class_calloc (ppw2->d_minus_product_11, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->d_minus_product_22, n_multipoles, sizeof(double), ppt2->error_message);    
    class_calloc (ppw2->d_plus_product_11, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->d_plus_product_22, n_multipoles, sizeof(double), ppt2->error_message);   

    class_calloc (ppw2->d_zero_product_12, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->d_zero_product_21, n_multipoles, sizeof(double), ppt2->error_message);   
    class_calloc (ppw2->d_zero_product_11, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->d_zero_product_22, n_multipoles, sizeof(double), ppt2->error_message);   

    class_calloc (ppw2->k_minus_product_12, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->k_minus_product_21, n_multipoles, sizeof(double), ppt2->error_message);    
    class_calloc (ppw2->k_plus_product_12, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->k_plus_product_21, n_multipoles, sizeof(double), ppt2->error_message);   

    class_calloc (ppw2->k_minus_product_11, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->k_minus_product_22, n_multipoles, sizeof(double), ppt2->error_message);    
    class_calloc (ppw2->k_plus_product_11, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->k_plus_product_22, n_multipoles, sizeof(double), ppt2->error_message);   

    class_calloc (ppw2->k_zero_product_12, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->k_zero_product_21, n_multipoles, sizeof(double), ppt2->error_message);   
    class_calloc (ppw2->k_zero_product_11, n_multipoles, sizeof(double), ppt2->error_message);
    class_calloc (ppw2->k_zero_product_22, n_multipoles, sizeof(double), ppt2->error_message);   

  }
  
  return _SUCCESS_;
  
}




int perturb2_workspace_free (
          struct perturbs2 * ppt2,
          struct background * pba,          
          struct perturb2_workspace * ppw2
          )
{

  free(ppw2->pvecback);
  free(ppw2->pvecthermo);
  free(ppw2->pvecmetric);

  free(ppw2->pvec_sources1);    
  free(ppw2->pvec_sources2);  

  /* Free quadratic sources temporary arrays */
  free(ppw2->pvec_quadsources);
  free(ppw2->pvec_quadcollision);

  /* Free quadratic sources table */
  for (int index_qs2=0; index_qs2<ppw2->qs2_size; ++index_qs2) {
    free (ppw2->quadsources_table[index_qs2]);
    free (ppw2->dd_quadsources_table[index_qs2]);
    free (ppw2->quadcollision_table[index_qs2]);
    free (ppw2->dd_quadcollision_table[index_qs2]);
  }
  free (ppw2->quadsources_table);
  free (ppw2->dd_quadsources_table);  
  free (ppw2->quadcollision_table);
  free (ppw2->dd_quadcollision_table);  
  

  /* Free coupling coefficient products */
  free(ppw2->c_minus_product_12);
  free(ppw2->c_minus_product_21);
  free(ppw2->c_plus_product_12);
  free(ppw2->c_plus_product_21);

  free(ppw2->c_minus_product_11);
  free(ppw2->c_minus_product_22);
  free(ppw2->c_plus_product_11);
  free(ppw2->c_plus_product_22);

  free(ppw2->r_minus_product_12);
  free(ppw2->r_minus_product_21);
  free(ppw2->r_plus_product_12);
  free(ppw2->r_plus_product_21);

  if (ppt2->has_polarization2) {

    free(ppw2->d_minus_product_12);
    free(ppw2->d_minus_product_21);
    free(ppw2->d_plus_product_12);
    free(ppw2->d_plus_product_21);

    free(ppw2->d_minus_product_11);
    free(ppw2->d_minus_product_22);
    free(ppw2->d_plus_product_11);
    free(ppw2->d_plus_product_22);

    free(ppw2->d_zero_product_12);
    free(ppw2->d_zero_product_21);
    free(ppw2->d_zero_product_11);
    free(ppw2->d_zero_product_22);

    free(ppw2->k_minus_product_12);
    free(ppw2->k_minus_product_21);
    free(ppw2->k_plus_product_12);
    free(ppw2->k_plus_product_21);

    free(ppw2->k_minus_product_11);
    free(ppw2->k_minus_product_22);
    free(ppw2->k_plus_product_11);
    free(ppw2->k_plus_product_22);

    free(ppw2->k_zero_product_12);
    free(ppw2->k_zero_product_21);
    free(ppw2->k_zero_product_11);
    free(ppw2->k_zero_product_22);

  }  

  /* Free arrays for the rotated multipoles */
  free(ppw2->rotation_1);
  free(ppw2->rotation_2);
  free(ppw2->rotation_1_minus);
  free(ppw2->rotation_2_minus);

  free (ppw2->approx);

  free(ppw2);

  return _SUCCESS_;
}


/**
  * This function is used by the Runge-Kutta integrator.  The RK integrator is used only for those modes
  * where the implicit ndf15 integrator fails.  Note that ndf15 does not need a timescale, as it determines
  * timescales automatically by computing the Jacobian of the system.
  */
int perturb2_timescale(
          double tau,
          void * parameters_and_workspace,
          double * timescale,
          ErrorMsg error_message
          )
{ 
            
  *timescale = 1;
            
  return _SUCCESS_;
  
}


/**
 * For a given wavemode (k1,k2,k3), find the number of time intervals bewteen
 * tau_ini and tau_end.
 *
 * The differential solver will be run once for each time interval. If all
 * approximations are disabled, there is only one time interval ranging from
 * tau_ini to tau_end. Each active approximation corresponds to an extra time
 * interval. For example, we might have:
 * 
 * - a time interval for the tight coupling approximation (TCA) between tau_ini
 *   and tau_rec;
 * - a time interval where no approximation is used between tau_rec and 
 *   z=100;
 * - a time interval where both radiation streaming approximations (RSA and
 *   UFA) are active between z=100 and today.
 *
 * The exact times where each approximation kicks in or out are determined in
 * the function perturb2_approximations().
 *
 * The weak point of this scheme is that it does not allow an approximation
 * to switch state more than once. For example, it is not possible to start
 * an approximation turned off, then activate it at some point, and finally
 * turn it off after a while. This is why CLASS requires the system to
 * start with the TCA turned on. For more detail on this point, see comment
 * in perturbations.h to `enum tca_flags`.
 *
 * Note that the reason of running the solver separately for each time interval
 * is that each approximations has its own set of evolution equations. For
 * example, during TCA only the first few photon multipoles are evolved.
 *
 * This function is basically the same of the CLASS function
 * perturb_find_approximation_number().
 */
int perturb2_find_approximation_number(
        struct precision * ppr,
        struct precision2 * ppr2,
        struct background * pba,
        struct thermo * pth,
        struct perturbs * ppt,
        struct perturbs2 * ppt2,
        struct perturb2_workspace * ppw2,
        double tau_ini, /**< input: initial time of the perturbation integration */
        double tau_end, /**< input: final time of the perturbation integration */
        int * interval_number, /**< output: total number of intervals */
        int * interval_number_of /**< output: number of intervals with respect to each particular approximation */
        )
{
  
  
  /* By default, there is only one time interval ranging from tau_ini to tau_end */
  *interval_number = 1; 

  /* Loop over all approximations to find whether they switch state between tau_ini and
  tau_end. The idea is that each approximation can be turned on or off only once.  Hence,
  if an approximation starts on, it can be turned off but after that it cannot be turned
  on again.  Similarly, an application that kicks is at a certain time cannot be turned
  off.  Hence, the approximation that are active can be counted just by checking whether
  their state (on or off) differs at tau_ini and tau_end. If it is the same state, then
  it must mean that the approximation is turned off. Note that two approximations can
  be turned on at the same time; this is indeed the case for the RSA and UFA approximations
  at late times.  */
  for (int index_ap = 0; index_ap < ppw2->ap2_size; ++index_ap) {

    /* Determine the state of the current approximation at the initial time,
    and memorise it in flag_ini */
    class_call(perturb2_approximations(
              ppr,
              ppr2,
              pba,
              pth,
              ppt,
              ppt2,
              tau_ini,
              ppw2),
         ppt2->error_message,
         ppt2->error_message);
    
    int flag_ini = ppw2->approx[index_ap];
    
    /* Determine the state of the current approximation at the final time,
    and memorise it in flag_end */
    class_call(perturb2_approximations(
              ppr,
              ppr2,
              pba,
              pth,
              ppt,
              ppt2,
              tau_end,
              ppw2),
         ppt2->error_message,
         ppt2->error_message);

    /* This can either be 0 or 1, because ppw2->approx is a logical array */
    int flag_end = ppw2->approx[index_ap];
    
    /* This test is meaningful only if the the various 'enum xxx_flags' enumerations
    are declared in cronological order (see the comment in perturbations.h).  */
    class_test(flag_end < flag_ini,
      ppt2->error_message,
      "For each approximation scheme, the declaration of approximation labels\
in the enumeration must follow chronological order, e.g: enum approx_flags\
{flag1, flag2, flag3} with flag1 being the initial one and flag3 the final one");
    
    /* If the current approximation switches state, then we need to run the evolver
    on an extra time interval. */
    *interval_number += flag_end - flag_ini;
    interval_number_of[index_ap] = flag_end - flag_ini + 1;
    
    /* Some debug */
    // printf("interval_number_of[%d] = %d\n", index_ap, interval_number_of[index_ap]);

  } // end of for (index_ap)
  
  return _SUCCESS_;
  
}




/**
 * For a given mode and wavenumber, find the time values where the various
 * approximations switch state (from on to off or viceversa).
 * 
 * This function is almost exactly equal to the default CLASS one in the
 * perturbations.c module. It fills the input arrays interval_limit and
 * interval_approx. The latter is a logical matrix
 * interval_approx[index_interval][index_ap] containing the state
 * of each approximation for a given time interval.
 */

int perturb2_find_approximation_switches (
          struct precision * ppr,
          struct precision2 * ppr2,
          struct background * pba,
          struct thermo * pth,
          struct perturbs * ppt,
          struct perturbs2 * ppt2,
          struct perturb2_workspace * ppw2,
          double tau_ini,
          double tau_end,
          double precision,
          int interval_number,
          int * interval_number_of,
          double * interval_limit, /**< output, should be already allocated with size=interval_number */
          int ** interval_approx   /**< output, logical matrix interval_approx[index_interval][index_ap]
                                        that contains which approximation is turned on for a given time
                                        interval (should be already allocated) */
          )
{

  /** - write in output arrays the initial time and approximation */

  interval_limit[0] = tau_ini;

  class_call(perturb2_approximations(
               ppr,
               ppr2,
               pba,
               pth,
               ppt,
               ppt2,
               tau_ini,
               ppw2),
    ppt2->error_message,
    ppt2->error_message);
  
  for (int index_ap=0; index_ap<ppw2->ap2_size; index_ap++)
    interval_approx[0][index_ap] = ppw2->approx[index_ap];
  
  /** - if there are no approximation switches, just write final time and return */

  if (interval_number == 1) {

    interval_limit[1] = tau_end;

  }
  
  /** - if there are switches, consider approximations one after each
  other.  Find switching time by bisection. Store all switches in
  arbitrary order in array unsorted_tau_switch[] */

  else {

    double * unsorted_tau_switch;
    class_alloc (unsorted_tau_switch, (interval_number-1)*sizeof(double), ppt2->error_message);

    int index_switch_tot = 0;

    for (int index_ap=0; index_ap < ppw2->ap2_size; index_ap++) {

      /* If there is a switch (on->off or off->on) in the current approximation,
        find the time of such switch */
      if (interval_number_of[index_ap] > 1) {

        double tau_min = tau_ini;

        /* flag_ini is either on or off */
        int flag_ini = interval_approx[0][index_ap];

        /* If we are indise this if-block, then num_switch must be 1 */
        int num_switch = interval_number_of[index_ap]-1;

        for (int index_switch=0; index_switch < num_switch; index_switch++) {
  
          double lower_bound = tau_min;
          double upper_bound = tau_end;
          double mid = 0.5*(lower_bound+upper_bound);

          while (upper_bound - lower_bound > precision) {
       
            class_call(perturb2_approximations(
                     ppr,
                     ppr2,
                     pba,
                     pth,
                     ppt,
                     ppt2,
                     mid,
                     ppw2),
                ppt2->error_message,
                ppt2->error_message);

            if (ppw2->approx[index_ap] > flag_ini+index_switch) {
              upper_bound=mid;
            }
            else {
              lower_bound=mid;
            }

            mid = 0.5*(lower_bound+upper_bound);

          }

          unsorted_tau_switch[index_switch_tot]=mid;
          index_switch_tot++;

          tau_min=mid;

        }
      }
    }

    class_test(index_switch_tot != (interval_number-1),
       ppt2->error_message,
       "bug in approximation switch search routine: should have %d = %d",
       index_switch_tot,interval_number-1);
    

    /** - now sort interval limits in correct order */
    
    index_switch_tot = 1;
    
    while (index_switch_tot < interval_number) {
      
      double next_tau_switch=tau_end;
      for (int index_switch=0; index_switch<interval_number-1; index_switch++) {
        if ((unsorted_tau_switch[index_switch] > interval_limit[index_switch_tot-1]) &&
            (unsorted_tau_switch[index_switch] < next_tau_switch)) {
          next_tau_switch=unsorted_tau_switch[index_switch];
        }
      }
      interval_limit[index_switch_tot]=next_tau_switch;
      index_switch_tot++;
    } // end of while(index_switch_tot)
    
    interval_limit[index_switch_tot]=tau_end;
    
    class_test(index_switch_tot != interval_number,
       ppt2->error_message,
       "most probably two approximation switching time were found to be\
equal, which cannot be handled\n");
    

    /** - store each approximation in chronological order */

    for (int index_switch=1; index_switch<interval_number; index_switch++) {
      
      class_call(perturb2_approximations(
                   ppr,
                   ppr2,
                   pba,
                   pth,
                   ppt,
                   ppt2,
                   0.5*(interval_limit[index_switch]+interval_limit[index_switch+1]),
                   ppw2),
        ppt2->error_message,
        ppt2->error_message);
      
      for (int index_ap=0; index_ap < ppw2->ap2_size; index_ap++) {

        interval_approx[index_switch][index_ap]=ppw2->approx[index_ap];

        /* check here that approximation does not go backward (remember that by definition the value
          of an approximation can only increase) */
        class_test(interval_approx[index_switch][index_ap] < interval_approx[index_switch-1][index_ap],
          ppt2->error_message,
          "The approximation with label %d is not defined correctly: it goes backward\
(from %d to %d) for (k1,k2,k)=(%g,%g,%g) and between tau=%e and %e; this cannot be handled\n",
          index_ap,
          interval_approx[index_switch-1][index_ap],
          interval_approx[index_switch][index_ap],
          ppw2->k1,
          ppw2->k2,
          ppw2->k,
          0.5*(interval_limit[index_switch-1]+interval_limit[index_switch]),
          0.5*(interval_limit[index_switch]+interval_limit[index_switch+1])
          );
      }

      /* check here that more than one approximation is not switched on at a given time */
      int num_switching_at_given_time=0;
      for (int index_ap=0; index_ap<ppw2->ap2_size; index_ap++)
        if (interval_approx[index_switch][index_ap] != interval_approx[index_switch-1][index_ap])
          num_switching_at_given_time++;

        class_test(num_switching_at_given_time != 1,
                   ppt2->error_message,
                   "for (k1,k2,k)=(%g,%g,%g), at tau=%g, you switch %d approximations\
at the same time, this cannot be handled. Usually happens in two cases: triggers for\
different approximations coincide, or one approx is reversible\n",
                   ppw2->k1,
                   ppw2->k2,
                   ppw2->k,
                   interval_limit[index_switch],
                   num_switching_at_given_time);

      /* Print information of switching times */
      if (ppt2->perturbations2_verbose > 3) {

        if ((interval_approx[index_switch-1][ppw2->index_ap2_tca]==(int)tca_on) && 
            (interval_approx[index_switch][ppw2->index_ap2_tca]==(int)tca_off))
          printf_log_if (ppt2->perturbations2_verbose, 3,
            "      \\ will switch off tight-coupling approximation at tau = %g\n",
            interval_limit[index_switch]);
  
        if ((interval_approx[index_switch-1][ppw2->index_ap2_rsa]==(int)rsa_off) && 
            (interval_approx[index_switch][ppw2->index_ap2_rsa]==(int)rsa_on))
          printf_log_if (ppt2->perturbations2_verbose, 3,
            "      \\ will switch on radiation streaming approximation at tau = %g\n",
            interval_limit[index_switch]);
  
        if (pba->has_ur == _TRUE_)
          if ((interval_approx[index_switch-1][ppw2->index_ap2_ufa]==(int)ufa_off) && 
              (interval_approx[index_switch][ppw2->index_ap2_ufa]==(int)ufa_on))
            printf_log_if (ppt2->perturbations2_verbose, 3,
            "      \\ will switch on ur fluid approximation at tau = %g\n",
              interval_limit[index_switch]);

        if ((interval_approx[index_switch-1][ppw2->index_ap2_nra]==(int)nra_off) && 
            (interval_approx[index_switch][ppw2->index_ap2_nra]==(int)nra_on))
          printf_log_if (ppt2->perturbations2_verbose, 3,
            "      \\ will switch on no-radiation approximation at tau = %g\n",
            interval_limit[index_switch]);

      } // end of if(verbose)

    } // end of for (index_switch)
  
    free(unsorted_tau_switch);

    class_call(perturb2_approximations(
                 ppr,
                 ppr2,
                 pba,
                 pth,
                 ppt,
                 ppt2,
                 tau_end,
                 ppw2),     
      ppt2->error_message,
      ppt2->error_message);

  } // end of if(interval_number!=1)

  return _SUCCESS_;

}








































/**
 * Determine which approximation are active at a given time and at a given
 * k-mode. 
 *
 * This function is the second-order equivalent of perturb_approximations()
 * in CLASS.
 */

int perturb2_approximations (
         struct precision * ppr,
         struct precision2 * ppr2,
         struct background * pba,
         struct thermo * pth,
         struct perturbs * ppt,
         struct perturbs2 * ppt2,
         double tau,
         struct perturb2_workspace * ppw2
         )
{

  /* Compute Fourier mode time scale */
  double tau_k = 1./MAX(ppw2->k,MAX(ppw2->k1,ppw2->k2));

  /* Interpolate background quantities */
  int dump;
  class_call (background_at_tau(
                pba,
                tau,
                pba->long_info, 
                pba->inter_normal, 
                &dump,
                ppw2->pvecback),
    pba->error_message,
    ppt2->error_message);

  double a = ppw2->pvecback[pba->index_bg_a];
  double Hc = a*ppw2->pvecback[pba->index_bg_H];

  /* Time scale of expansion */
  double tau_h = 1./Hc;

  /* Interpolate thermodynamical quantities */
  class_call (thermodynamics_at_z(pba,
                pth,
                1/ppw2->pvecback[pba->index_bg_a]-1,  /* redshift z=1./a-1 */
                pth->inter_normal,
                &dump,
                ppw2->pvecback,
                ppw2->pvecthermo),
    pth->error_message,
    ppt2->error_message);

  double kappa_dot = ppw2->pvecthermo[pth->index_th_dkappa];

  /* Recombination time scale, same as mean free time of a photon */
  double tau_c = 1/kappa_dot;

  /* Initialise the number of active approximations */
  ppw2->n_active_approximations = 0;

  /* Uncomment to always turn off all approximations and be done with it */
  // ppw2->approx[ppw2->index_ap2_tca] = (int)tca_off;
  // ppw2->approx[ppw2->index_ap2_rsa] = (int)rsa_off;
  // ppw2->approx[ppw2->index_ap2_nra] = (int)nra_off;
  // return _SUCCESS_;


  // ========================================================================================
  // =                              Tight coupling approximation                            =
  // ========================================================================================

  /* Always turn the TCA off if the user asked for tca2_none  */
  if (ppt2->tight_coupling_approximation == tca2_none) {
    ppw2->approx[ppw2->index_ap2_tca] = (int)tca_off;
  }
  /* Otherwise, check whether tight-coupling approximation should be on */
  else if ((tau_c/tau_h < ppt2->tight_coupling_trigger_tau_c_over_tau_h) &&
           (tau_c/tau_k < ppt2->tight_coupling_trigger_tau_c_over_tau_k)) {
    ppw2->approx[ppw2->index_ap2_tca] = (int)tca_on;
    ppw2->n_active_approximations++;
  }
  else {
    ppw2->approx[ppw2->index_ap2_tca] = (int)tca_off;
  }


  // ========================================================================================
  // =                              Free streaming approximation                            =
  // ========================================================================================

  /* TO BE IMPLEMENTED YET */

  // if ((tau/tau_k > ppr->radiation_streaming_trigger_tau_over_tau_k) &&
  //     (tau > pth->tau_free_streaming) &&
  //     (ppr->radiation_streaming_approximation != rsa_none)) {
  //
  //   ppw2->approx[ppw2->index_ap2_rsa] = (int)rsa_on;
  //   ppw2->n_active_approximations++;
  // }
  // else {
  //   ppw2->approx[ppw2->index_ap2_rsa] = (int)rsa_off;
  // }
  //
  // if (pba->has_ur == _TRUE_) {
  //
  //   if ((tau/tau_k > ppr->ur_fluid_trigger_tau_over_tau_k) &&
  //       (ppr->ur_fluid_approximation != ufa_none)) {
  //
  //     ppw2->approx[ppw2->index_ap2_ufa] = (int)ufa_on;
  //     ppw2->n_active_approximations++;
  //   }
  //   else {
  //     ppw2->approx[ppw2->index_ap2_ufa] = (int)ufa_off;
  //   }
  // }
  //
  // /* The UFA is basically radiation streaming for neutrinos. The condition for triggering it
  // is essentially the same: turn it on only if the wavemode is well inside the horizon. 
  // The difference between RSA and UFA relies in the fact that neutrinos do not care
  // about recombination, hence the UFA can be turned on also during or before recombination */
  // if (pba->has_ncdm == _TRUE_) {
  //
  //   if ((tau/tau_k > ppr->ncdm_fluid_trigger_tau_over_tau_k) &&
  //       (ppr->ncdm_fluid_approximation != ncdmfa_none)) {
  //
  //     ppw2->approx[ppw2->index_ap2_ncdmfa] = (int)ncdmfa_on;
  //     ppw2->n_active_approximations++;
  //   }
  //   else {
  //     ppw2->approx[ppw2->index_ap2_ncdmfa] = (int)ncdmfa_off;
  //   }
  // }


  // ========================================================================================
  // =                               No radiation approximation                             =
  // ========================================================================================
  
  /* Treat radiation as a perfect fluid after equality, when its contribution to the
  total density becomes negligible. This is the poor man's version of the free streaming
  approximation, meant to reduce the execution time rather than to compute the photon
  monopole and dipole quickly. */

  /* Always turn the NRA off if the user asked for nra_none  */
  if (ppt2->no_radiation_approximation == nra2_none) {
    ppw2->approx[ppw2->index_ap2_nra] = (int)nra_off;
  }
  /* Turn on the no-radiation approximation only if we are well after equality,
  when the matter to radiation density ratio becomes larger than 
  ppt2->no_radiation_approximation_rho_m_over_rho_r */  
  else {
    if ((a/pba->a_eq) > ppt2->no_radiation_approximation_rho_m_over_rho_r) {
      ppw2->approx[ppw2->index_ap2_nra] = (int)nra_on;
      ppw2->n_active_approximations++;
      /* Debug - Check that the NRA approximation is turned on when we want */
      // printf ("a, a_eq, ratio = %g, %g, %g\n", a, pba->a_eq, a/pba->a_eq);
    }
    else {
      ppw2->approx[ppw2->index_ap2_nra] = (int)nra_off;
    }
  }
  

  // ========================================================================================
  // =                                Compatibility checks                                  =
  // ========================================================================================

  class_test ((ppw2->approx[ppw2->index_ap2_nra]==(int)nra_on)
    && (ppw2->approx[ppw2->index_ap2_tca]==(int)tca_on),
    ppt2->error_message,
    "tau=%g: the TCA and NRA approximations can't be turned on at the same time.",
    tau);
    
  class_test (ppw2->n_active_approximations>1,
    ppt2->error_message,
    "tau=%g: so far SONG only supports one active approximation at the same time.",
    tau);

  return _SUCCESS_;
}







/**
 * Update the geometrical quantities related to the current (k1,k2,k3) wavemode
 * being evolved.
 *
 * Most of the formulas used below are described in detail in Appendices A and
 * B of http://arxiv.org/abs/1405.2280.
 *
 * Here we compute many useful quantities related to the system of wavevectors
 * \vec{k}, \vec{k1}, \vec{k2}, and store them in the workspace ppw2.  Contrary
 * to first order, at second order a perturbation in \vec{k} is given by a convolution
 * integral over two dummy wavevectors, \vec{k1} and \vec{k2}, that sum up to \vec{k}.
 * That is, the following must be fulfilled at all times:
 *
 *    kx1 + kx2 = kx,    ky1 + ky2 = ky,     kz1 + kz2 = kz.
 *
 * Due to the symmetries described in Appendix B of http://arxiv.org/abs/1405.2280,
 * we have some freedom in choosing our set of (k1, k2, k). In SONG, we choose
 * the following:
 *
 * -# we align the k vector with the polar axis of our reference frame (z axis), which
 *    coincides with the symmetry axis of the spherical harmonics Y_lm(theta, phi). This
 *    implies that:
 *        kx1 + kx2 = 0,    ky1 + ky2 = 0,    kz1 + kz2 = |k|
 *    where |k| is the magnitude of k.  This choice greatly simplifies the system of
 *    differential equation, effectively decoupling the evolution of multipoles with
 *    different azimuthal number m.
 *
 * -# we impose that both k1 and k2 lie in the x-z axis:
 *        ky1 = ky2 = 0.
 *    This implies that the azimuthal angle phi of the two wavevectors either vanishes
 *    or is equal to pi, making all of our equations real-valued (see last paragraph
 *    of sec. 4.2 of Pitrou et al. 2010).
 *
 * Here we also compute the rotation coefficients that rotate the first-order
 * perturbations to arbitrary wavemode configurations. For details on this
 * part of the function, refer to the in-code documentation pr to appendix B
 * and sec. 6.2.1.1 of http://arxiv.org/abs/1405.2280.
 *
 * This function is called early in perturb2_solve(), which in turn is called for
 * each (k1,k2,k) triplet.
 * 
 */ 
int perturb2_geometrical_corner (
        struct precision * ppr,
        struct precision2 * ppr2,
        struct background * pba,
        struct thermo * pth,
        struct perturbs * ppt,
        struct perturbs2 * ppt2,
        int index_k1,
        int index_k2,
        int index_k3,                    
        struct perturb2_workspace * ppw2
        )
{

  // =================================================================================
  // =                                 Compute angles                                =
  // =================================================================================

  ppw2->index_k1 = index_k1;
  ppw2->index_k2 = index_k2;
  ppw2->index_k3 = index_k3;

  /* Get the Fourier modes */
  double k1 = ppw2->k1 = ppt2->k[index_k1];
  double k2 = ppw2->k2 = ppt2->k[index_k2];
  double k = ppw2->k = ppt2->k3[index_k1][index_k2][index_k3];
  double k_sq = ppw2->k_sq = k*k;

 	if ((ppt2->k3_sampling == sym_k3_sampling) ) {
 	
 		/*in sym sampling the index refers to the transformed k's while k1,k2,k are the actual k's*/
 		k1 = ppw2->k1 = (ppt2->k[index_k2] + ppt2->k3[index_k1][index_k2][index_k3])/2.;
  	k2 = ppw2->k2 = (ppt2->k[index_k1] + ppt2->k3[index_k1][index_k2][index_k3])/2.;
  	k = ppw2->k = 	(ppt2->k[index_k1] + ppt2->k[index_k2])/2.;
  	k_sq = ppw2->k_sq = k*k;
 	
 	}

  /* Get the angles */
  double cosk1k2 = ppw2->cosk1k2 = (k_sq - k1*k1 - k2*k2)/(2.*k1*k2);
  double cosk1k = ppw2->cosk1k = (k1 + k2*cosk1k2)/k;
  double cosk2k = ppw2->cosk2k = (k2 + k1*cosk1k2)/k;
  double sink1k = sqrt (1-cosk1k*cosk1k);
  double sink2k = sqrt (1-cosk2k*cosk2k);
  double theta_1 = ppw2->theta_1 = acos (cosk1k);
  double theta_2 = ppw2->theta_2 = acos (cosk2k);

  /* Scalar product between \vec{k1} and \vec{k2} */
  double k1_dot_k2 = ppw2->k1_dot_k2 = k1*k2*cosk1k2;
  
  /* One can use alternative formulas for the sines and cosines */
  // double cosk1k = ppw2->cosk1k = (k_sq + k1*k1 - k2*k2)/(2*k1*k);
  // double cosk2k = ppw2->cosk2k = (k_sq + k2*k2 - k1*k1)/(2*k2*k);
  // double sink1k = ppw2->sink1k = sin (theta_1);
  // double sink2k = ppw2->sink2k = sin (theta_2);
  /* and the result would be slightly different because the differential system
  solved will be slightly different due to the different round-up error. The
  difference is of the order of the precision of the system (1e-6 for a normal
  run) */

  /* Check that all cosines are smaller than one. */
  class_test (fabs(cosk1k2)>1, ppt2->error_message,
    "mode (%g,%g,%g,%g), cosk1k2 = %.17g is larger than one\n",
    k1, k2, k, cosk1k2, cosk1k2);

  class_test (fabs(cosk1k)>1, ppt2->error_message,
    "mode (%g,%g,%g,%g), cosk1k = %.17g is larger than one\n",
    k1, k2, k, cosk1k2, cosk1k);

  class_test (fabs(cosk2k)>1, ppt2->error_message,
    "mode (%g,%g,%g,%g), cosk2k = %.17g is larger than one\n",
    k1, k2, k, cosk1k2, cosk2k);
    
  /* The sines must be positive because the zenith angle goes from 0 to pi */
  class_test (sink1k < 0, ppt2->error_message,
    "mode (%g,%g,%g,%g), sink1k = %.17g is too small\n",
    k1, k2, k, cosk1k2, sink1k);

  class_test (sink2k < 0, ppt2->error_message,
    "mode (%g,%g,%g,%g), sink2k = %.17g is too small\n",
    k1, k2, k, cosk1k2, sink2k);
  

  // ==========================================================================================
  // =                             Compute rotation coefficients                              =
  // ==========================================================================================

  /* The first-order perturbations enter the second-order system as quadratic sources.
  We have computed them in perturbations.c, but only for those configurations where 
  the k wavemode is aligned with the polar axis.
  
  However, to complete the second-order system, we need these first-order perturbations
  to be evaluated in arbitrary k-configurations, not only aligned to the polar axis.
  The statistical isotropy of the Universe allows us to obtain the arbitrary perturbations
  by a simple rotation of the polar-aligned perturbations.
  
  In the general case, the rotation coefficients are given by Wigner rotation matrices.
  In our case, we adopt the following simplifications:
    - the first-order perturbations are only scalar,
    - we assume that k1 and k2 lay on the x-z plane, that is, that their azimuthal
      angle phi is either 0 or pi.
  Then, the rotation matrices reduce to Legendre polynomials P_lm; for more detail,
  see sec. B.1 of http://arxiv.org/abs/1405.2280.
  
  We store the rotation coefficients in the array ppw2->rotation_1 for k1 and in
  ppw2->rotation_1 for k2.
  
  The rotations coefficients are the same regardless of the considered species. The
  monopoles (delta_g, delta_b, ...) do not need to be rotated. The dipoles are rotated
  by multiplication with k1[m] or k2[m]. For example, the rotation of the first-order
  velocity is given by this simple formula:
     v_m(k1) = i k1_m * vpot(k1)
     v_m(k2) = i k2_m * vpot(k2), 
  where vpot is the scalar potential of the irrotation velocity: v^i = dv/dx^i;
  the formulas are equivalent to eq. A.38 of Pitrou et al. 2010. Things get more
  complicated for higher moments.
  
  For more detail on rotations and statistical isotropy see appendix B and sec.
  6.2.1.1 of http://arxiv.org/abs/1405.2280see, eq. A.6 of Beneke, Fidler &
  Klingmuller 2011, eq. A.37 of Pitrou et al. 2010. */
    
  for (int l=0; l<=ppt2->largest_l_quad; ++l) {
    
    for (int m=0; m<=l; ++m) {

      /* The rotation coefficients for the first-order quantities are defined as
          sqrt(4pi/(2l+1)) Y_lm(theta,phi)
      and they appear in the the rotation formula (eq. B.9 of http://arxiv.org/abs/1405.2280)
          Delta_lm(\vec{k1}) = sqrt(4pi/(2l+1)) Y_lm(theta,phi) Delta_l(k1)
      where Delta_l is the first-order multipole computed with k1 aligned with the zenith.
      Since Delta_l is real, Delta_lm under complex conjugation behaves like the spherical
      harmonic:
          Delta_lm = (-1)^m Delta*_l-m.
      Since we choose phi_1=0 and phi_2=pi, Y_lm(theta,phi) is real too. Then we have that
          Delta_lm = (-1)^m Delta_l-m.
      This justifies the (-1)^m factor that we include below.

      The spherical harmonics are given by Y_lm(theta,phi) = P_lm(costheta) exp(i*m*phi).
      Since we choose phi_1=0 and phi_2=pi, the rotation coefficients for \vec{k2} have
      an extra (-1)^m factor arising from the identity exp(i*m*pi) = (-1)^m. */
      if (ppt2->rescale_quadsources == _FALSE_) { 
        ppw2->rotation_1[lm_quad(l,m)] = plegendre_lm(l,m,cosk1k);
        ppw2->rotation_2[lm_quad(l,m)] = ALTERNATING_SIGN(m) * plegendre_lm(l,m,cosk2k);
        ppw2->rotation_1_minus[lm_quad(l,m)] = ALTERNATING_SIGN(m) * ppw2->rotation_1[lm_quad(l,m)];
        ppw2->rotation_2_minus[lm_quad(l,m)] = ALTERNATING_SIGN(m) * ppw2->rotation_2[lm_quad(l,m)];
      }

      /* Divide the rotation coefficients by a factor sin(theta_k1)^m, if asked. This is the
      same as rescaling the transfer functions by the same factor. The pow(k1/k2) factors
      come from the fact that sin(theta_k2) = k1/k2 sin(theta_k1). Note that for m<0, we multiply
      the  coefficients by sin(theta_1)^|m|. We do so because in order to compute the bispectrum
      of the CMB we need the function T_lm / Y_mm(k1) which is proportional to
      T_lm / sin(theta_k1)^|m| (see sec. 6.2.1.2 of http://arxiv.org/abs/1405.2280). Because of the
      presence of the absolute value, |m|, the rescaling that we perform here corresponds to Y_mm
      only for m>=0. This is not a problem because we need the transfer functions for m>=0 in any
      case. */

      else {
        ppw2->rotation_1[lm_quad(l,m)] = plegendre_lm_rescaled(l,m,cosk1k);
        ppw2->rotation_2[lm_quad(l,m)] =
          (ALTERNATING_SIGN(m) * plegendre_lm_rescaled(l,m,cosk2k)) * pow(k1/k2,m);
        ppw2->rotation_1_minus[lm_quad(l,m)] = plegendre_lm_rescaled(l,-m,cosk1k);
        ppw2->rotation_2_minus[lm_quad(l,m)] =
          (ALTERNATING_SIGN(m) * plegendre_lm_rescaled(l,-m,cosk2k)) * pow(k1/k2,-m);
      }
      
      /* Check that the rotation coefficients for positive and negative m's coincide for m=0 */
      if (m==0) {
        class_test ((fabs(1-ppw2->rotation_1[lm_quad(l,m)]/ppw2->rotation_1_minus[lm_quad(l,m)]))
         ||(fabs(1-ppw2->rotation_2[lm_quad(l,m)]/ppw2->rotation_2_minus[lm_quad(l,m)])),
          ppt2->error_message,
          "error in computation of rotation coefficients: minus and plus do not match at m=0");
      }

      
      /* Sqrt(4*pi/(2*l+1)) factor from the definition of the Wigner rotation (see
      Beneke & Fidler 2011, eq. A.6,  or Pitrou et al. 2010 eq. A.37). */    
      double pre_factor = sqrt(4*_PI_/(2*l+1));
      ppw2->rotation_1[lm_quad(l,m)] *= pre_factor;
      ppw2->rotation_2[lm_quad(l,m)] *= pre_factor;
      ppw2->rotation_1_minus[lm_quad(l,m)] *= pre_factor;
      ppw2->rotation_2_minus[lm_quad(l,m)] *= pre_factor;

      /* Debug the values of the rotation coefficients */
      if (ppt2->perturbations2_verbose > 4) {
        if ((index_k1==14) && (index_k2==12) && (index_k3==4)) {
          printf("Rotation coefficients at (l,m)=(%3d,%3d), with lm_quad(l,m)=%3d: ",
            l, m, lm_quad(l,m));
          printf("rot1 = %+10.4g(%+10.4g),  ",
            ppw2->rotation_1[lm_quad(l,m)], ppw2->rotation_1_minus[lm_quad(l,m)]);
          printf("rot2 = %+10.4g(%+10.4g)\n" ,
            ppw2->rotation_2[lm_quad(l,m)], ppw2->rotation_2_minus[lm_quad(l,m)]);
        }
      }
 
    }  // end of cycle on 'm'
  } // end of cycle on 'l'
  
  
  // ==================================================================================
  // =                            Define spherical coordinatesn                       =
  // ==================================================================================
  
  /* Spherical coordinates of the Fourier modes \vec{k1} and \vec{k2}.
  
  Note that when rescaling is turned on, these quantities are not anymore the spherical
  coordinates. */
  double k1_M1 = ppw2->k1_m[-1 +1] = k1*rot_1(1,-1);
  double k1_0  = ppw2->k1_m[ 0 +1] = k1*rot_1(1, 0);
  double k1_P1 = ppw2->k1_m[+1 +1] = k1*rot_1(1,+1);
  
  double k2_M1 = ppw2->k2_m[-1 +1] = k2*rot_2(1,-1);
  double k2_0  = ppw2->k2_m[ 0 +1] = k2*rot_2(1, 0);
  double k2_P1 = ppw2->k2_m[+1 +1] = k2*rot_2(1,+1);


  /* Tensorial product between \vec{k1} and \vec{k2}, given by
      X[m]^ij k1_i k2_j,
  where the X matrix can be found in eq. A.13 of Beneke & Fidler 2010 or
  in eq. A.52 of http://arxiv.org/abs/1405.2280. We can also compute it
  by using the equality 
      X[m]^ij k1_i k2_j = c_minus_12(2,m) k1*k2,
  and we do so after we compute the coupling coefficients, below. Note that
  when rescaling is turned on, these quantities are the tensorial
  products divided by sin(theta)^m. */

  /* X[m]^ij k1_i k2_j */
  ppw2->k1_ten_k2[-2 +2] = sqrt_2/sqrt_3 * k1_M1 * k2_M1;               // m = -2
  ppw2->k1_ten_k2[-1 +2] = (k1_M1*k2_0 + k2_M1*k1_0)/sqrt_3;            // m = -1
  ppw2->k1_ten_k2[ 0 +2] = k1_0*k2_0 - k1_dot_k2/3.;                    // m =  0
  ppw2->k1_ten_k2[+1 +2] = (k1_P1*k2_0 + k2_P1*k1_0)/sqrt_3;            // m =  1
  ppw2->k1_ten_k2[+2 +2] = sqrt_2/sqrt_3 * k1_P1*k2_P1;                 // m =  2
  
  /* X[m]^ij k1_i k1_j */
  ppw2->k1_ten_k1[-2 +2] = sqrt_2/sqrt_3 * k1_M1 * k1_M1;               // m = -2
  ppw2->k1_ten_k1[-1 +2] = (k1_M1*k1_0 + k1_M1*k1_0)/sqrt_3;            // m = -1
  ppw2->k1_ten_k1[ 0 +2] = k1_0*k1_0 - k1*k1/3.;                        // m =  0
  ppw2->k1_ten_k1[+1 +2] = (k1_P1*k1_0 + k1_P1*k1_0)/sqrt_3;            // m =  1
  ppw2->k1_ten_k1[+2 +2] = sqrt_2/sqrt_3 * k1_P1*k1_P1;                 // m =  2
  
  /* X[m]^ij k2_i k2_j */
  ppw2->k2_ten_k2[-2 +2] = sqrt_2/sqrt_3 * k2_M1 * k2_M1;               // m = -2
  ppw2->k2_ten_k2[-1 +2] = (k2_M1*k2_0 + k2_M1*k2_0)/sqrt_3;            // m = -1
  ppw2->k2_ten_k2[ 0 +2] = k2_0*k2_0 - k2*k2/3.;                        // m =  0
  ppw2->k2_ten_k2[+1 +2] = (k2_P1*k2_0 + k2_P1*k2_0)/sqrt_3;            // m =  1
  ppw2->k2_ten_k2[+2 +2] = sqrt_2/sqrt_3 * k2_P1*k2_P1;                 // m =  2


  // ==========================================================================================
  // =                             Store coupling coefficients                                =
  // ==========================================================================================
  
  /* Store the (l,m,s) transforms of the terms
  i \vec{k_1}\cdot\vec{n} f(\vec{k_1})
  i \vec{k_1}\cdot\vec{n} f(\vec{k_2})
  i \vec{k_2}\cdot\vec{n} f(\vec{k_1})
  i \vec{k_2}\cdot\vec{n} f(\vec{k_2}) .
  
  This can be done without knowledge of the distribution function f because, at first order, it
  is just given by
  f_{lm}(\vec{k}) = \sqrt(4\pi/(2l+1)) Y_{lm}(\vec{k}) \tilde{f}(k) ,
  where \tilde{f}(k) is f as computed with \vec{k} aligned with the zenith. */

  for (int l=0; l<=ppt2->largest_l; ++l) {
    for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

      int m = ppt2->m[index_m];

      /* Initialize inner products (they will be accumulated) */
      
      ppw2->c_minus_product_12[lm(l,m)] = ppw2->c_minus_product_21[lm(l,m)] = 0;
      ppw2->c_plus_product_12[lm(l,m)]  = ppw2->c_plus_product_21[lm(l,m)]  = 0;
      ppw2->c_minus_product_11[lm(l,m)] = ppw2->c_minus_product_22[lm(l,m)] = 0;
      ppw2->c_plus_product_11[lm(l,m)]  = ppw2->c_plus_product_22[lm(l,m)]  = 0;

      if (ppt2->has_polarization2) {

        ppw2->d_minus_product_12[lm(l,m)] = ppw2->d_minus_product_21[lm(l,m)] = 0;
        ppw2->d_plus_product_12[lm(l,m)]  = ppw2->d_plus_product_21[lm(l,m)]  = 0;
        ppw2->d_minus_product_11[lm(l,m)] = ppw2->d_minus_product_22[lm(l,m)] = 0;
        ppw2->d_plus_product_11[lm(l,m)]  = ppw2->d_plus_product_22[lm(l,m)]  = 0;

        ppw2->d_zero_product_12[lm(l,m)] = ppw2->d_zero_product_21[lm(l,m)] = 0;
        ppw2->d_zero_product_11[lm(l,m)] = ppw2->d_zero_product_22[lm(l,m)] = 0;
      }

        
      /* Fill the products by summing over m2 */
      
      for (int m2=-1; m2<=1; ++m2) {

        int m1 = m-m2;

        /* Define shorthands for the coupling coefficients. This is the only place where the arrays like
        ppt2->c_minus are used */
        double c_minus = ppt2->c_minus[lm(l,m)][m-m1+1];
        double c_plus = ppt2->c_plus[lm(l,m)][m-m1+1];
        double d_minus = ppt2->d_minus[lm(l,m)][m-m1+1];
        double d_plus = ppt2->d_plus[lm(l,m)][m-m1+1];
        double d_zero = ppt2->d_zero[lm(l,m)][m-m1+1];


        /* Intensity couplings */
        ppw2->c_minus_product_12[lm(l,m)] += rot_1(1,m2) * rot_2(l-1,m1) * c_minus;
        ppw2->c_minus_product_21[lm(l,m)] += rot_2(1,m2) * rot_1(l-1,m1) * c_minus;
        ppw2->c_plus_product_12[lm(l,m)]  += rot_1(1,m2) * rot_2(l+1,m1) * c_plus;
        ppw2->c_plus_product_21[lm(l,m)]  += rot_2(1,m2) * rot_1(l+1,m1) * c_plus;
        ppw2->c_minus_product_11[lm(l,m)] += rot_1(1,m2) * rot_1(l-1,m1) * c_minus;
        ppw2->c_minus_product_22[lm(l,m)] += rot_2(1,m2) * rot_2(l-1,m1) * c_minus;
        ppw2->c_plus_product_11[lm(l,m)]  += rot_1(1,m2) * rot_1(l+1,m1) * c_plus;
        ppw2->c_plus_product_22[lm(l,m)]  += rot_2(1,m2) * rot_2(l+1,m1) * c_plus;

        if (ppt2->has_polarization2) {

          /* E-mode polarization couplings */
          ppw2->d_minus_product_12[lm(l,m)] += rot_1(1,m2) * rot_2(l-1,m1) * d_minus;
          ppw2->d_minus_product_21[lm(l,m)] += rot_2(1,m2) * rot_1(l-1,m1) * d_minus;
          ppw2->d_plus_product_12[lm(l,m)]  += rot_1(1,m2) * rot_2(l+1,m1) * d_plus;
          ppw2->d_plus_product_21[lm(l,m)]  += rot_2(1,m2) * rot_1(l+1,m1) * d_plus;
          ppw2->d_minus_product_11[lm(l,m)] += rot_1(1,m2) * rot_1(l-1,m1) * d_minus;
          ppw2->d_minus_product_22[lm(l,m)] += rot_2(1,m2) * rot_2(l-1,m1) * d_minus;
          ppw2->d_plus_product_11[lm(l,m)]  += rot_1(1,m2) * rot_1(l+1,m1) * d_plus;
          ppw2->d_plus_product_22[lm(l,m)]  += rot_2(1,m2) * rot_2(l+1,m1) * d_plus;

          /* B-mode polarization couplings. Note that there is no l-1 or l+1 here because
            the d_zero mix E and B modes. */
          ppw2->d_zero_product_12[lm(l,m)]  += rot_1(1,m2) * rot_2(l,m1) * d_zero;
          ppw2->d_zero_product_21[lm(l,m)]  += rot_2(1,m2) * rot_1(l,m1) * d_zero;
          ppw2->d_zero_product_11[lm(l,m)]  += rot_1(1,m2) * rot_1(l,m1) * d_zero;
          ppw2->d_zero_product_22[lm(l,m)]  += rot_2(1,m2) * rot_2(l,m1) * d_zero;
          
        }
      } // end of for (m2)

      /* The R couplings (eq. 142 of BF 2010) are obtained from the C ones */
      ppw2->r_minus_product_12[lm(l,m)] =  (l-1) * ppw2->c_minus_product_12[lm(l,m)];
      ppw2->r_minus_product_21[lm(l,m)] =  (l-1) * ppw2->c_minus_product_21[lm(l,m)];
      ppw2->r_plus_product_12[lm(l,m)]  = -(l+2) * ppw2->c_plus_product_12[lm(l,m)];
      ppw2->r_plus_product_21[lm(l,m)]  = -(l+2) * ppw2->c_plus_product_21[lm(l,m)];


      if (ppt2->has_polarization2) {
        
        /* The K couplings (eq. 142 of BF 2010) are obtained from the D ones */
        ppw2->k_minus_product_12[lm(l,m)] =  (l-1) * ppw2->d_minus_product_12[lm(l,m)];
        ppw2->k_minus_product_21[lm(l,m)] =  (l-1) * ppw2->d_minus_product_21[lm(l,m)];
        ppw2->k_plus_product_12[lm(l,m)]  = -(l+2) * ppw2->d_plus_product_12[lm(l,m)];
        ppw2->k_plus_product_21[lm(l,m)]  = -(l+2) * ppw2->d_plus_product_21[lm(l,m)];
        ppw2->k_minus_product_11[lm(l,m)] =  (l-1) * ppw2->d_minus_product_11[lm(l,m)];
        ppw2->k_minus_product_22[lm(l,m)] =  (l-1) * ppw2->d_minus_product_22[lm(l,m)];
        ppw2->k_plus_product_11[lm(l,m)]  = -(l+2) * ppw2->d_plus_product_11[lm(l,m)];
        ppw2->k_plus_product_22[lm(l,m)]  = -(l+2) * ppw2->d_plus_product_22[lm(l,m)];
        ppw2->k_zero_product_12[lm(l,m)]  = -ppw2->d_zero_product_12[lm(l,m)];
        ppw2->k_zero_product_21[lm(l,m)]  = -ppw2->d_zero_product_21[lm(l,m)];
        ppw2->k_zero_product_11[lm(l,m)]  = -ppw2->d_zero_product_11[lm(l,m)];
        ppw2->k_zero_product_22[lm(l,m)]  = -ppw2->d_zero_product_22[lm(l,m)];
      }

      /* Some debug */
      // if ( (index_k1 == 0) && (index_k2 == 0) && (index_cosk1k2 == 1) ) {
      //    printf("~~~~~ Inner products at (l,index_m)=(%3d,%3d), with lm(l,m)=%3d: \n", l, m, lm(l,m));
      //    printf("c_minus_product_12 = %+10.4g ,   ", ppw2->c_minus_product_12[lm(l,m)]);
      //    printf("c_minus_product_21 = %+10.4g\n"   , ppw2->c_minus_product_21[lm(l,m)]);
      //    printf("Produp12 = %+10.4g ,   ", ppw2->c_plus_product_12[lm(l,m)]*(2*l+3));
      //    printf("Produp21 = %+10.4g\n"   , ppw2->c_plus_product_21[lm(l,m)]*(2*l+3));
      //    printf("Proddown12 = %+10.4g ,   ", ppw2->c_minus_product_12[lm(l,m)]*(2*l-1));
      //    printf("Proddown21 = %+10.4g\n"   , ppw2->c_minus_product_21[lm(l,m)]*(2*l-1));
      //    printf("\n");
      // }

    } // end of for (m)
  } // end of for (l)


  /* Test the c_minus factor when l=2 by checking that the equality
  X[m]^ij k1_i k2_j = c_minus_12(2,m) k1*k2 holds. */
  /* TODO: check analytically whether the equality should hold!!! */
  /* TODO: compare with ratios instead than absolute difference
  using http://floating-point-gui.de/errors/comparison/ */
  for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
    
    int m = ppr2->m[index_m];

    double k1_ten_k2_cminus = (k1*k2*c_minus_12(2,m));
    class_test_permissive (fabs (k1_ten_k2_cminus - ppw2->k1_ten_k2[m+2]) > _SMALL_,
      ppt2->error_message,
      "mode (%g,%g,%g,%g), m=%d, k1_ten_k2 check failed!\n%20.12g != %20.12g, diff = %g",
      k1, k2, k, cosk1k2, m, k1_ten_k2_cminus, ppw2->k1_ten_k2[m+2],
      fabs (1-(k1_ten_k2_cminus)/(ppw2->k1_ten_k2[m+2])));

    double k1_ten_k1_cminus = (k1*k1*c_minus_11(2,m));
    class_test_permissive (fabs (k1_ten_k1_cminus - ppw2->k1_ten_k1[m+2]) > _SMALL_,
      ppt2->error_message,
      "mode (%g,%g,%g,%g), m=%d, k1_ten_k1 check failed!\n%20.12g != %20.12g, diff = %g",
      k1, k2, k, cosk1k2, m, k1_ten_k1_cminus, ppw2->k1_ten_k1[m+2],
      fabs (1-(k1_ten_k1_cminus)/(ppw2->k1_ten_k1[m+2])));

    double k2_ten_k2_cminus = (k2*k2*c_minus_22(2,m));
    class_test_permissive (fabs (k2_ten_k2_cminus - ppw2->k2_ten_k2[m+2]) > _SMALL_,
      ppt2->error_message,
      "mode (%g,%g,%g,%g), m=%d, k2_ten_k2 check failed!\n%20.12g != %20.12g, diff = %g",
      k1, k2, k, cosk1k2, m, k2_ten_k2_cminus, ppw2->k2_ten_k2[m+2],
      fabs (1-(k2_ten_k2_cminus)/(ppw2->k2_ten_k2[m+2])));
  }


  // ============================================================================
  // =                                Print info                                =
  // ============================================================================

  /* Print some basic information about the wavemodes */
  if (ppt2->perturbations2_verbose > 2)
    printf(" -> Now considering mode (k1,k2,k,cosk1k2)=(%8.4g,%8.4g,%8.4g,%8.4g), indices=(%3d,%3d,%3d)\n",
      k1, k2, k, cosk1k2, index_k1, index_k2, index_k3);

  /* Print some advanced information about the wavemodes */
  if (ppt2->perturbations2_verbose > 4) {
    
    printf("     * Geometrical quantities associated with the mode:\n");
    printf("       %12.9s%12.9s%12.9s%12.9s%12.9s%12.9s%12.9s\n",
      "k", "cosk1k2", "sink1k", "sink2k", "cosk1k", "cosk2k", "k1.k2");
    printf("       %12.5g%12.5g%12.5g%12.5g%12.5g%12.5g%12.5g\n",
      k,cosk1k2,sink1k,sink2k,cosk1k,cosk2k,ppw2->k1_dot_k2);
    printf("       In spherical coordinates:\n");
    printf("       %12.9s%12.9s%12.9s%12.9s%12.9s%12.9s\n",
      "k1_0", "k2_0", "k1_P1", "k2_P1", "k1_M1", "k2_M1");
    printf("       %12.5g%12.5g%12.5g%12.5g%12.5g%12.5g\n",
      k1_0, k2_0, k1_P1, k2_P1, k1_M1, k2_M1);
  }


  // ==========================================================================================
  // =                              Useful checks on the geometry                             =
  // ==========================================================================================

  /* Nice check on the geometry of the system: k1*sin(theta_1) = k2*sin(theta_2). Since
  the quantities can be very close to zero, we only ask for a moderate precision */
  // class_test (fabs (1 - (k1*sink1k)/(k2*sink2k)) > 1e4*_SMALL_,
  class_test ((k1*sink1k - k2*sink2k) > 1e4*_SMALL_,
    ppt2->error_message,
    "geometry check failed!\n%20.12g != %20.12g, diff = %g, sink1k=%g, sink2k=%g",
    k2/k1, sink1k/sink2k, fabs (1-(k2/k1)/(sink1k/sink2k)), sink1k, sink2k);

  /* Check that kz1 + kz2 = k (it follows from \vec{k1}+\vec{k2} = \vec{k}) */
  double kz1 = k1*cosk1k;
  double kz2 = k2*cosk2k;

  class_test (
    fabs(k - (kz1+kz2)) > _SMALL_,
    ppt2->error_message,
    "mode (%.17f,%.17f,%.17f): kz1 + kz2 != k (%.17f + %.17f != %.17f), there must be an error in the computation of the wavemodes angles.", 
      ppw2->k1, ppw2->k2, ppw2->k, kz1, kz2, k);

  /* Check that kx1 + kx2 = 0 (it follows from \vec{k1}+\vec{k2} = \vec{k}) */
  double kx1 = k1*sqrt(1.-cosk1k*cosk1k);
  double kx2 = -k2*sqrt(1.-cosk2k*cosk2k);

  class_test (
    fabs(kx1+kx2) > _SMALL_,
    ppt2->error_message,
    "mode (%.17f,%.17f,%.17f): kx1 != -kx2 (kx1=%.17f, kx2=%.17f), there must be an error in the computation of the wavemodes angles.", 
      ppw2->k1, ppw2->k2, ppw2->k, kx1, kx2);

  /* Check that the l=1 rotation coefficients for the k-wavemode is equal to unit k-vector
  in spherical coordinates. This is just a property of spherical harmonics. */
  double k1_0_cartesian = kz1;
  double k2_0_cartesian = kz2;
  double k1_P1_cartesian = -kx1/sqrt_2;
  double k2_P1_cartesian = -kx2/sqrt_2;
  
  /* Apply the rescaling (in order to compare with our rescaled k1[m] and k2[m]) */
  if (ppt2->rescale_quadsources == _TRUE_) {
    k1_P1_cartesian = k1_P1_cartesian / sink1k;
    k2_P1_cartesian = k2_P1_cartesian / sink2k * k1/k2;
  }
  
  class_test(
    fabs(1-k1_0_cartesian/k1_0) > _SMALL_ || fabs(1-k1_P1_cartesian/k1_P1) > _SMALL_ ||
    fabs(1-k2_0_cartesian/k2_0) > _SMALL_ || fabs(1-k2_P1_cartesian/k2_P1) > _SMALL_,  
    ppt2->error_message,
    "mode (%.17f,%.17f,%.17f): the L=1 rotation coefficient are not correct.",
      ppw2->k1, ppw2->k2, ppw2->k);

  /* Check that the scalar product is independent of the adopted k-coordinates. Note that
  this test does not need to be corrected for the rescaling because the scalar-product
  (being a scalar) is invariant under the rescaling. */
  double k1_dot_k2_spherical = k1_0*k2_0 - k1_M1*k2_P1 - k1_P1*k2_M1;
  
  class_test (
    fabs(k1_dot_k2_spherical - ppw2->k1_dot_k2) > _SMALL_,
    ppt2->error_message,
    "mode (%.17f,%.17f,%.17f): the scalar product k1.k2 changes between spherical and cartesian coordinates (%.17f != %.17f).",
    ppw2->k1, ppw2->k2, ppw2->k, ppw2->k1_dot_k2, k1_dot_k2_spherical);

  /* Check the monopole of the 'plegendre_lm' routine.  The monopole is not affected by the rotation,
  hence rotation_1[0] should be equal to one regardless of the considered wavemodes. */
  class_test(fabs(ppw2->rotation_1[lm_quad(0,0)]-1.) > _SMALL_,
    ppt2->error_message,
    "mode (%.17f,%.17f,%.17f): the L=0 rotation coefficient is different from 1 (%.17f != %.17f). There must be a mistake in the way the geometrical quantities are computed.",
      ppw2->k1, ppw2->k2, ppw2->k, ppw2->rotation_1[lm_quad(0,0)], 1.);

  /* Check that the the inner coupling with c_minus is zero for l=m=0.  We need this to
  be true, otherwise the monopole hierarchy would be coupled to the l=-1 moment. */
  if (ppr2->compute_m[0]==_TRUE_)
    class_test (
      (fabs(ppw2->c_minus_product_12[lm(0,0)])>_SMALL_) || (fabs(ppw2->c_minus_product_21[lm(0,0)])>_SMALL_),
      ppt2->error_message,
      "mode (%.17f,%.17f,%.17f): found c_minus(l=0,m=0) !=  0 (%.17f != 0 or %.17f != 0). There must be a mistake in the way the geometrical quantities are computed.",
        ppw2->k1, ppw2->k2, ppw2->k, ppw2->c_minus_product_12[lm(0,0)], ppw2->c_minus_product_21[lm(0,0)]);

  /* Check that the the inner product with c_minus for L=1, m=0 is equal to rot_1*rot_2. This
  has to be the case because c_minus(1,m1,0) != 0 only if m1 = 0. */
  if (ppr2->compute_m[0]==_TRUE_)
    class_test (
      (fabs(1-ppw2->c_minus_product_12[lm(1,0)]/(ppw2->rotation_1[lm_quad(1,0)]*ppw2->rotation_2[lm_quad(0,0)]))>_SMALL_) ||
      (fabs(1-ppw2->c_minus_product_21[lm(1,0)]/(ppw2->rotation_2[lm_quad(1,0)]*ppw2->rotation_1[lm_quad(0,0)]))>_SMALL_),
      ppt2->error_message,
      "mode (%.17f,%.17f,%.17f): found wrong value for c_minus(l=1,m=0). There must be a mistake in the way the geometrical quantities are computed.",
        ppw2->k1, ppw2->k2, ppw2->k);

  /* Check that the d_zero-inner-product for m=0 (scalar modes) vanishes.  This is equivalent
  to test that B-mode polarization at second order does not exist for m=0, as it is clear from
  eq. 2.17 of BFK. */
  if (ppr2->compute_m[0]==_TRUE_)
    if (ppt2->has_polarization2 == _TRUE_)
      for (int l=0; l<=ppt2->largest_l; ++l)
        class_test ( fabs(ppw2->d_zero_product_12[lm(l,0)]) > _SMALL_,
          ppt2->error_message,
          "mode (%.17f,%.17f,%.17f): found d_zero_product(l,0) != 0. There must be a mistake in the way the geometrical quantities are computed.",
          ppw2->k1, ppw2->k2, ppw2->k);

  /* Check that the scaling that we have applied separately on rot_1 and rot_2 is
  equivalent to an overall scaling of 1/sin(theta)^m. This is a very important test
  on the rescaling method in general and, in particualr, on the (k1/k2)^m factor
  appearing in the rescaling of rot_2. */
  if (ppt2->rescale_quadsources == _TRUE_) {

    /* Computed the non-rescaled k1[m] */
    double k1_M1 = kx1/sqrt_2;
    double k1_0  = kz1;
    double k1_P1 = -k1_M1;  
      
    /* Computed the non-rescaled k2[m] */
    double k2_M1 = kx2/sqrt_2;
    double k2_0  = kz2;
    double k2_P1 = -k2_M1;

    /* Compute the non-rescaled k1_ten_k2[m] */
    double k1_ten_k2[5];
    k1_ten_k2[-2 +2] = sqrt_2/sqrt_3 * k1_M1 * k2_M1;               // m = -2
    k1_ten_k2[-1 +2] = (k1_M1*k2_0 + k2_M1*k1_0)/sqrt_3;            // m = -1
    k1_ten_k2[ 0 +2] = k1_0*k2_0 - k1_dot_k2/3.;                    // m =  0
    k1_ten_k2[+1 +2] = (k1_P1*k2_0 + k2_P1*k1_0)/sqrt_3;            // m =  1
    k1_ten_k2[+2 +2] = sqrt_2/sqrt_3 * k1_P1*k2_P1;                 // m =  2    

    /* Check that the scaling that we have applied separately on rot_1 and rot_2 is
    equivalent to an overall scaling of 1/sin(theta)^m */    
    for (int m=0; m <= 2; ++m) {
      
      /* Apply the inverse scaling to the scaled tensor product */
      double k1_ten_k2_unrescaled = ppw2->k1_ten_k2[m+2] * pow (sink1k, m);

      if (fabs(k1_ten_k2_unrescaled) > _MINUSCULE_)
        class_test (fabs (k1_ten_k2[m+2] - k1_ten_k2_unrescaled) > _SMALL_,
          ppt2->error_message,
          "check failed!\n%20.12g != %20.12g, diff = %g",
          k1_ten_k2[m+2], k1_ten_k2_unrescaled,
          fabs (1-(k1_ten_k2[m+2])/(k1_ten_k2_unrescaled)));

    }    
  } // end of tests on rescaled coefficients
  
  /* There will be plenty of divisions in what follows. */
  class_test((k1==0.) || (k2==0.) || (k_sq==0.),
    ppt2->error_message,
    "mode (%.17f,%.17f,%.17f): stop to avoid division by zero", ppw2->k1, ppw2->k2, ppw2->k);  
 
  return _SUCCESS_;
  
}




/**
 * Simple function to fill the ppw2->info string with useful information about the wavemode
 * that is currently being integrated
 */
int perturb2_wavemode_info (
        struct precision * ppr,
        struct precision2 * ppr2,
        struct background * pba,
        struct thermo * pth,
        struct perturbs * ppt,
        struct perturbs2 * ppt2,
        struct perturb2_workspace * ppw2
        )
{

  /*  We shall write on ppw2->info line by line, commenting each line with the below
  comment style. */
  char comment[2] = "#";
  char line[1024];
  char * info = ppw2->info;
  
  sprintf(info, "");

  sprintf(line, "k1 = %g, k2 = %g, k = %g, cosk1k2 = %g, theta_1 = %g",
    ppw2->k1, ppw2->k2, ppw2->k, ppw2->cosk1k2, ppw2->theta_1);
  sprintf(info, "%s%s %s\n", info, comment, line);

  if (ppt->gauge == newtonian) sprintf(line, "gauge = newtonian");
  if (ppt->gauge == synchronous) sprintf(line, "gauge = synchronous");
  sprintf(info, "%s%s %s\n", info, comment, line);

  return _SUCCESS_;

}





/**
 * Solve the second-order differential system for the considered wavemodes (k1,k2,k3).
 *
 * This function is called in the loop over the three wavemodes (k1,k2,k3). It calls
 * the differential evolver over the different time intervals; in turn the evolver
 * evolves the second-order perturbations and stores the line of sight sources
 * in the ppt2->sources array.
 *
 * In detail, this function does:
 *
 * -# Compute important geometrical quantities related to the wavemodes and 
 *    the coupling coefficients entering the Boltzmann equation, in 
 *    perturb2_geometrical_corner().
 *
 * -# Determine the time when to start evolving the system for the considered
 *    wavemode set.
 *
 * -# Determine which approximations will be activated for the current
 *    wavemode, and based on them split the integration range in several
 *    time intervals, via perturb2_find_approximation_number() and
 *    perturb2_find_approximation_switches().
 *
 * -# Tabulate the terms quadratic in the first-order perturbations that
 *    enter the second-order differential system, in view of later interpolation,
 *    in perturb2_quadratic_sources_for_k1k2k().
 *
 * -# Loop over the integration intervals and for each of them determine which
 *    equations to evolve and set their initial condition, in
 *    perturb2_vector_init().
 *
 * -# Finally, for each time interval solve the system by calling the differential
 *    solver, which is either an implicit solver (evolver_ndf15) or an explicit 
 *    Runge-Kutta one (evolver_rk). By default, SONG uses the former.
 *
 */
int perturb2_solve (
        struct precision * ppr,
        struct precision2 * ppr2,
        struct background * pba,
        struct thermo * pth,
        struct perturbs * ppt,
        struct perturbs2 * ppt2,
        int index_k1,
        int index_k2,
        int index_k3,                    
        struct perturb2_workspace * ppw2
        )
{

  // ====================================================================================
  // =                                 Update workspace                                 =
  // ====================================================================================

  /* Compute useful geometrical quantities related to the current (k1,k2,k3) wavemode */
  class_call (perturb2_geometrical_corner(
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2,
                index_k1,
                index_k2,
                index_k3,
                ppw2),
    ppt2->error_message,
    ppt2->error_message);

  /* Update Fourier modes */
  double k1 = ppw2->k1;
  double k2 = ppw2->k2;
  double k = ppw2->k;
  double k_sq = ppw2->k_sq;
  double cosk1k2 = ppw2->cosk1k2;
  
  /* Store some useful information in ppw2->info for debugging purposes */
  class_call (perturb2_wavemode_info (
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2,
                ppw2),
    ppt2->error_message,
    ppt2->error_message);
        
  /* Should we create debug files for this particular (k1,k2,k3)? */
  ppw2->print_function = NULL;
  
  if ((ppt2->has_debug_files == _TRUE_)
     && (index_k1 == ppt2->index_k1_debug)
     && (index_k2 == ppt2->index_k2_debug)
     && (index_k3 == ppt2->index_k3_debug))
    ppw2->print_function = perturb2_save_early_transfers;

  /* Initialize indices relevant for back/thermo tables search */
  ppw2->last_index_back=0;
  ppw2->last_index_thermo=0;
  ppw2->last_index_sources=0;  

  /* Initialise the counter of time steps in the differential system */
  ppw2->n_steps = 0;
  
  /* Reset the counter that keeps track of the number of calls of the
  function perturb2_derivs() */
  ppw2->derivs_count = 0;

  /* Overall structure containing all parameters, to be passed to the evolver
  which, in turn, will pass it to the perturb2_derivs() and perturb2_sources()
  functions. */
  struct perturb2_parameters_and_workspace ppaw2;
  ppaw2.ppr = ppr;
  ppaw2.ppr2 = ppr2;
  ppaw2.pba = pba;
  ppaw2.pth = pth;
  ppaw2.ppt = ppt;
  ppaw2.ppt2 = ppt2;
  ppaw2.ppw2 = ppw2;


  // ====================================================================================
  // =                               Determine tau_ini                                  =
  // ====================================================================================
  
  /* Determine when to start evolving the differential system for the current wavemode
  and store the conformal time value in ppw2->tau_start_evolution. Do it automatically
  if the user specified tau_start_evolution==0. */
  
  if (ppr2->custom_tau_start_evolution == 0) {

    class_call (perturb2_start_time_evolution (
                  ppr,
                  ppr2,
                  pba,
                  pth,
                  ppt,
                  ppt2,
                  MAX(MAX(k1,k2),k),
                  &ppw2->tau_start_evolution),
      ppt2->error_message,
      ppt2->error_message);
  }
  
  /* If the user specified a custom value for ppr2->custom_tau_start_evolution, use
  it as a common starting time  regardless of the wavemode */
  
  else {
  
    ppw2->tau_start_evolution = ppr2->custom_tau_start_evolution;
  
  }

  /* The starting integration time should be larger than the minimum time for which
  we sampled the first-order system */
  class_test (ppw2->tau_start_evolution < ppt->tau_sampling_quadsources[0],
    ppt2->error_message,
    "tau_ini for the second-order system should be larger than the time\
 where we start to sample the 1st-order quantities");

  /* The initial integration time should be smaller than the lowest time
  were we need to sample the line-of-sight sources */    
  class_test (ppw2->tau_start_evolution > ppt2->tau_sampling[0],
    ppt2->error_message,
    "tau_ini shoud be larger than first point in the sources time sampling");

  /* Print information on the starting integration time */
  printf_log_if (ppt2->perturbations2_verbose, 3,
    "     * evolution starts at tau=%g\n", ppw2->tau_start_evolution);


  // ====================================================================================
  // =                             Determine time intervals                             =
  // ====================================================================================

  /** Following CLASS example, we split the time range [tau_ini, tau_end] in time intervals
  according to the number of active approximation schemes. The differential solver needs to
  be run separately for each time interval, because each approximations has its own set of
  evolved equations. For example, during TCA only the first few photon multipoles are evolved.
  If all approximations are disabled, then there is only one time interval. Each active
  approximation results in an extra time interval. */

  /* Number of time intervals */
  int interval_number;

  /* Number of time intervals where each particular approximation is uniform.  It is either
  equal to one (when the approximation never changes its state i.e. it is always turned ON
  or OFF), or equal to two (when the approximation changes its state, either from ON to OFF
  or from OFF to ON). */
  int * interval_number_of;
  class_alloc (interval_number_of, ppw2->ap2_size*sizeof(int), ppt2->error_message);

  /* Determine interval_number and interval_number_of */
  class_call(perturb2_find_approximation_number(
               ppr,
               ppr2,
               pba,
               pth,
               ppt,
               ppt2,
               ppw2,
               ppw2->tau_start_evolution,
               ppt2->tau_sampling[ppt2->tau_size-1],
               &interval_number,
               interval_number_of),
    ppt2->error_message,
    ppt2->error_message);

  /* Edge of the time intervals. The first and last elements are tau_ini and tau_end. */
  double * interval_limit;
  class_alloc (interval_limit,(interval_number+1)*sizeof(double),ppt2->error_message);

  /* Logical matrix interval_approx[index_interval][index_ap] telling us which
  approximations are active for a given time interval. */
  int ** interval_approx;

  class_alloc (interval_approx,interval_number*sizeof(int*),ppt2->error_message);  
  for (int index_interval=0; index_interval < interval_number; index_interval++)
    class_alloc (interval_approx[index_interval], ppw2->ap2_size*sizeof(int), ppt2->error_message);

  /* Determine interval_limit and interval_approx */ 
  class_call (perturb2_find_approximation_switches(
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2,
                ppw2,
                ppw2->tau_start_evolution,
                ppt2->tau_sampling[ppt2->tau_size-1],
                ppr->tol_tau_approx,
                interval_number,
                interval_number_of,
                interval_limit,
                interval_approx),
    ppt2->error_message,
    ppt2->error_message);

  

  // ====================================================================================
  // =                               Tabulate quadsources                               =
  // ====================================================================================

  /* Compute the quadratic sources for the second-order system and tabulate them as 
  a function of time. */
  if (ppt2->has_quadratic_sources == _TRUE_) {
  
    if (ppt2->perturbations2_verbose > 3)
      printf("     * computing quadratic sources for the considered mode\n");
  
    class_call(perturb2_quadratic_sources_for_k1k2k(
              ppr,
              ppr2,
              pba,
              pth,            
              ppt,
              ppt2,
              ppw2
              ),
          ppt2->error_message,
          ppt2->error_message);
  }



  // ====================================================================================
  // =                                Solve the system                                  =
  // ====================================================================================
  
  /* Loop over the time intervals and, for each interval, solve the differential system */
  
  for (int index_interval=0; index_interval < interval_number; index_interval++) {

    /* Let the workspace know about the approximations which are turned on over the
    considered time interval */
    for (int index_ap=0; index_ap < ppw2->ap2_size; index_ap++)
      ppw2->approx[index_ap] = interval_approx[index_interval][index_ap];

    /* Find out which approximation schemes were active in the previous time interval.
    This is important because the initial conditions for this time interval depend
    on the last time step of the previous interval. */
    int * previous_approx;

    if (index_interval==0) {
      /* If the current interval is the first one, then set previous_approx=NULL, so that
      the function perturb2_vector_init() knows that the perturbations must be initialized
      with primordial initial conditions */
      previous_approx = NULL;
    }
    else {
      previous_approx = interval_approx[index_interval-1];
    }



    // ----------------------------------------------------------------------------------
    // -                            Set initial conditions                              -
    // ----------------------------------------------------------------------------------

    /* Determine which perturbations need to be evolved over the current time interval,
    and compute their initial conditions. If the current interval is the first one,
    use primordial initial conditions. If it starts from an approximation switching point,
    redistribute correctly the perturbations from the previous to the new vector of
    perturbations. */

    class_call (perturb2_vector_init(
                  ppr,
                  ppr2,
                  pba,
                  pth,
                  ppt,
                  ppt2,
                  interval_limit[index_interval],
                  ppw2,
                  previous_approx),
     ppt2->error_message,
     ppt2->error_message);
 


    // ----------------------------------------------------------------------------------
    // -                              Evolve the system                                 -
    // ----------------------------------------------------------------------------------

    /* Determine the evolver to use, based on the user's choice. The default choice is the
    implicit solver ndf15 by Thomas Tram (thanks!) */
    int (*generic_evolver)();
    extern int evolver_rk();
    extern int evolver_ndf15();   

    if(ppr->evolver == rk)
      generic_evolver = evolver_rk;
    else
      generic_evolver = evolver_ndf15;

    /* Solve the differential system over the current time interval */
    class_call (generic_evolver(
                  perturb2_derivs,
                  interval_limit[index_interval],
                  interval_limit[index_interval+1],
                  ppw2->pv->y,
                  ppw2->pv->used_in_sources,
                  ppw2->pv->pt2_size,
                  &ppaw2,
                  ppr2->tol_perturb_integration_song,
                  ppr->smallest_allowed_variation,
                  perturb2_timescale,                 /* Not needed when using the ndf15 integrator */
                  ppr->perturb_integration_stepsize,  /* Not needed when using the ndf15 integrator */
                  ppt2->tau_sampling,
                  ppt2->tau_size,
                  perturb2_sources,
                  ppw2->print_function,
                  what_if_ndf15_fails,  /* Exit strategy */
                  ppt2->error_message),
      ppt2->error_message,
      ppt2->error_message);


  } // end of for (index_interval)



  // ====================================================================================
  // =                                   Free memory                                    =
  // ====================================================================================

  class_call (perturb2_vector_free (ppw2->pv),
     ppt2->error_message,
     ppt2->error_message);
     
   for (int index_interval=0; index_interval < interval_number; index_interval++)
    free (interval_approx[index_interval]);
  free (interval_approx);
  free (interval_limit); 
  free(interval_number_of);

  return _SUCCESS_;
          
}
          
          
          
          









/**
 * Assign which perturbations should be evolved for the considered time interval,
 * based on the active approximations, and compute their initial values.
 *
 * This function is called just before the evolver is run on a new time interval.
 * It has the purpose to determine what needs to be evolved in the new time
 * interval. The number and extent of the various time intervals was previously
 * determined in perturb2_solve() using the perturb2_approximations() function.
 * 
 * In detail, this function:
 *
 * -# Initialises the pt2 indices, which correspond to the evolved quantites,
 *    based on which approximations are active in the next time interval
 *
 * -# Set the initial conditions for the considered time interval. For the first
 *    time interval, the IC are computed directly from inflation with the
 *    perturb2_initial_conditions() function. Otherwise, they are just 
 *    copied from the last step of the previous time interval.
 *
 * The evolver makes a call to perturb2_derivs() right before calling this
 * function, meaning that all the quantities in the ppw2 workspace are up to
 * date with the time tau.
 *
 * For mode detail on this function, especially on old_approx, refer to the
 * documentation of perturb_vector_init() in perturbations.c.
 *
 * IMPORTANT: do not break the hierarchies.  If you have to add another variable to
 * be evolved, make sure that you associate to it a pt2 index that is not in the middle
 * of a Boltzmann hierarchy.  For example, if you put your new variable between
 * 'index_pt2_monopole_g' and 'index_pt2_monopole_g+1', you will break the hierarchy
 * and generate unpredictable numerical errors.  This happens because the dy vector
 * is filled assuming that the dipole follows the monopole, the shear follows the dipole,
 * l3 follows the shear, and all the other l_max-3 moments follow l3 in a sequential way.
 */
int perturb2_vector_init (
      struct precision * ppr,
      struct precision2 * ppr2,
      struct background * pba,
      struct thermo * pth,
      struct perturbs * ppt,
      struct perturbs2 * ppt2,
      double tau, /**< input: starting time for the new time interval */
      struct perturb2_workspace * ppw2, /**< input/output: workspace containing the active
                                        approximation schemes in the new time interval,
                                        the background/thermodynamics/metric quantitites,
                                        and the vector y from the last time step of the
                                        previous time interval, used to set initial
                                        conditions for the new interval; and in output the
                                        new vector y */
      int * old_approx /**< input: NULL if we need to set y to initial conditions for a new
                       wavenumber; otherwise, a logical array with the active approximations
                       in the previous time interval */
      )
{

  /* Create a temporary perturb2_vector structure. We shall fill it below according
  to what equations need to be evolved in the new time interval, and eventually
  use it to replace the old structure in ppw2->pv. */
  struct perturb2_vector * ppv;
  class_alloc (ppv, sizeof(struct perturb2_vector), ppt2->error_message);

  /* Shortcut to the active approximation schemes */
  int * new_approx = ppw2->approx;  

  /* By default, evolve a closure relation for each of the massless species 
  in order to reduce the numerical noise. */
  ppv->use_closure_g = _TRUE_;
  ppv->use_closure_pol_g = _TRUE_;
  ppv->use_closure_ur = _TRUE_;


  // ====================================================================================
  // =                                     Checks                                       =
  // ====================================================================================

  /* For each relativistic species, check that we evolve at least the monopole, dipole
  and quadrupole. Also check that we have computed enough first-order multipoles to solve
  the second-order system. */

  /* Photon temperature */
  class_test (ppr2->l_max_g < 2,
    ppt2->error_message,
    "ppr2->l_max_g should be at least 3.");

  class_test (ppr2->l_max_g + ppt2->lm_extra > ppr->l_max_g,
    ppt2->error_message,
    "to solve the second-order photon hierarchy up to l=%d, you need to specify l_max_g > %d",
    ppr2->l_max_g, ppr2->l_max_g+3);

  /* Photon polarization */
  if (ppt2->has_polarization2 == _TRUE_) {
    
    class_test (ppr2->l_max_pol_g < 2,
      ppt2->error_message,
      "ppr2->l_max_pol_g should be at least 3.");
    
    class_test (ppr2->l_max_pol_g + ppt2->lm_extra > ppr->l_max_pol_g,
      ppt2->error_message,
      "to solve the second-order photon hierarchy up to l=%d, you need to specify l_max_pol_g > %d",
      ppr2->l_max_pol_g, ppr2->l_max_pol_g+ppt2->lm_extra);
  }
    
  /* Neutrinos */
  if (pba->has_ur == _TRUE_) {
    
    class_test(ppr2->l_max_ur < 2,
      ppt2->error_message,
      "ppr2->l_max_ur should be at least 3.");
     
    class_test (ppr2->l_max_ur + ppt2->lm_extra > ppr->l_max_ur,
      ppt2->error_message,
      "to solve the second-order photon hierarchy up to l=%d, you need to specify l_max_ur > %d",
      ppr2->l_max_ur, ppr2->l_max_ur+ppt2->lm_extra);
  }



  // ====================================================================================
  // =                             Equations to be evolved                              =
  // ====================================================================================

  /* By default, the number of equations evolved for the radiation species is determined
  directly by the user via the parameter file (ppr2->l_max_g) */
  ppv->l_max_g = ppr2->l_max_g;
  ppv->n_hierarchy_g = size_l_indexm (ppv->l_max_g, ppt2->m, ppt2->m_size);

  /* From the physical point of view, the E and B-mode hierarchies start from the quadrupole.
  However, we treat the polarization hierarchies as if it they started from the monopole,
  because it is simpler to implement in SONG. We thus introduce a fake monopole and dipole
  that are not really evolved but are taken to be always zero. */
  if (ppt2->has_polarization2 == _TRUE_) {
    ppv->l_max_pol_g = ppr2->l_max_pol_g;
    ppv->n_hierarchy_pol_g = size_l_indexm (ppv->l_max_pol_g, ppt2->m, ppt2->m_size);
  }
  
  if (pba->has_ur == _TRUE_) {
    ppv->l_max_ur = ppr2->l_max_ur; 
    ppv->n_hierarchy_ur = size_l_indexm (ppv->l_max_ur, ppt2->m, ppt2->m_size);
  }

  /* The radiation hierarchies (photon intensity and polarisation, neutrinos) are
  affected by the tight coupling and radiation streaming approximations. These
  approximations allow us to truncate the Boltzmann hierarchy and evolve a smaller
  number of equations. We account for this cutoff by changing the ppv->l_max_XXX
  parameter. */

  /* If the no_radiation_approximation is turned on, we need to evolve fewer equations */
  if (new_approx[ppw2->index_ap2_nra] == (int)nra_on) {

    /* Do not evolve relativistic species at all if we are adopting the method nra2_all.
    We take the l_max's to be equal to -1 so that a cycle starting from zero on l will
    not even enter the first iteration. */
    if (ppt2->no_radiation_approximation == (int)nra2_all) {

      ppv->l_max_g = -1;
      ppv->n_hierarchy_g = 0;
      ppv->use_closure_g = _FALSE_;
      
      ppv->l_max_pol_g = -1;
      ppv->n_hierarchy_pol_g = 0;
      ppv->use_closure_pol_g = _FALSE_;
      
      ppv->l_max_ur = -1;
      ppv->n_hierarchy_ur = 0;
      ppv->use_closure_ur = _FALSE_;
    }
    
    /* Treat the relativistic species as perfect fluid (only monopole and dipole) if
    we are adopting nra2_fluid */
    else if (ppt2->no_radiation_approximation == (int)nra2_fluid) {

      ppv->l_max_g = 1;
      ppv->n_hierarchy_g = size_l_indexm (ppv->l_max_g, ppt2->m, ppt2->m_size);
      ppv->use_closure_g = _FALSE_;
    
      ppv->l_max_pol_g = 1;
      ppv->n_hierarchy_pol_g = size_l_indexm (ppv->l_max_pol_g, ppt2->m, ppt2->m_size);
      ppv->use_closure_pol_g = _FALSE_;
      
      ppv->l_max_ur = 1; 
      ppv->n_hierarchy_ur = size_l_indexm (ppv->l_max_ur, ppt2->m, ppt2->m_size);
      ppv->use_closure_ur = _FALSE_;
      
      /* Note that 'size_l_indexm' will return 0 if we are only computing m-values larger
      than 1. This is perfectly fine, as we do not evolve anything in that case. */
      
    }
  } // end of if (nra_on)
  
  
  /* If the tight coupling approximation is turned on, we only need to evolve the photon
  monopole and the baryon monopole and dipole, as all other moments, including polarisation,
  can be computed analytically. */
  if (new_approx[ppw2->index_ap2_tca] == (int)tca_on) {

    ppv->l_max_g = 0;
    ppv->n_hierarchy_g = size_l_indexm (ppv->l_max_g, ppt2->m, ppt2->m_size);
    ppv->use_closure_g = _FALSE_;

    ppv->l_max_pol_g = -1;
    ppv->n_hierarchy_pol_g = size_l_indexm (ppv->l_max_pol_g, ppt2->m, ppt2->m_size);
    ppv->use_closure_pol_g = _FALSE_;

  } // end of if (tca_on)


  /* We shall increment this index to count the equations to evolve */
  int index_pt = 0;

  
  // ------------------------------------------------------------------------------------
  // -                                Photon temperature                                -
  // ------------------------------------------------------------------------------------

  ppv->index_pt2_monopole_g = index_pt;
  index_pt += ppv->n_hierarchy_g;

  if (ppt2->perturbations2_verbose > 4)
    printf("     * photon temperature hierarchy: we shall evolve %d equations (l_max_g = %d).\n",
      ppv->n_hierarchy_g, ppv->l_max_g);


  // ------------------------------------------------------------------------------------
  // -                               Photon polarisation                                -
  // ------------------------------------------------------------------------------------
      
  if (ppt2->has_polarization2 == _TRUE_) {

    /* E-modes */
    ppv->index_pt2_monopole_E = index_pt;
    index_pt += ppv->n_hierarchy_pol_g;

    /* B-modes */
    ppv->index_pt2_monopole_B = index_pt;
    index_pt += ppv->n_hierarchy_pol_g;

    if (ppt2->perturbations2_verbose > 4)
      printf("     * photon E-mode & B-mode hierarchies: we shall evolve %d equations (l_max_pol_g = %d).\n",
        2*ppv->n_hierarchy_pol_g, ppv->l_max_pol_g);
  }


  // ------------------------------------------------------------------------------------
  // -                                    Neutrinos                                     -
  // ------------------------------------------------------------------------------------
  
  if (pba->has_ur == _TRUE_) {

    ppv->index_pt2_monopole_ur = index_pt;
    index_pt += ppv->n_hierarchy_ur;

    if ((pba->has_ur) && (ppt2->perturbations2_verbose > 4))
      printf("     * neutrino hierarchy: we shall evolve %d equations (l_max_ur = %d).\n",
        ppv->n_hierarchy_ur, ppr2->l_max_ur);
  }


  // ------------------------------------------------------------------------------------
  // -                                     Baryons                                      -
  // ------------------------------------------------------------------------------------

  /* Evolve a reduced set of equations if the baryon fluid is treated as perfect.
  Otherwise, include the pressure (n=2,l=0,m=0) and the anisotropic stresses (n=2,l=2,m)
  moments in the hierarchy. */
  if (ppt2->has_perfect_baryons == _FALSE_) {
    ppv->n_max_b = 2;
    ppv->l_max_b = 2;
  }
  else {
    ppv->n_max_b = 1;
    ppv->l_max_b = 1;
  }

  ppv->n_hierarchy_b = size_n_l_indexm (ppv->n_max_b, ppv->l_max_b, ppt2->m, ppt2->m_size);
  ppv->index_pt2_monopole_b = index_pt;
  index_pt += ppv->n_hierarchy_b;
  
  if (ppt2->perturbations2_verbose > 4)
    printf("     * baryon hierarchy: we shall evolve %d equations.\n",
      ppv->n_hierarchy_b);


  // ------------------------------------------------------------------------------------
  // -                                Cold dark matter                                  -
  // ------------------------------------------------------------------------------------



  // ==============================================
  // =               magnetic fields              =
  // ==============================================
	if (ppt2->has_magnetic_field == _TRUE_) {
		ppv->index_pt2_monopole_mag = index_pt;
		index_pt += size_l_indexm (1, ppt2->m, ppt2->m_size);
	}
	
  // ==============================================
  // =               Cold Dark Matter             =
  // ==============================================

  if (pba->has_cdm == _TRUE_) {

    /* See comment above for baryons */
    if (ppt2->has_perfect_cdm == _FALSE_) {
      ppv->n_max_cdm = 2;
      ppv->l_max_cdm = 2;
    }
    else {
      ppv->n_max_cdm = 1;
      ppv->l_max_cdm = 1;
    }

    ppv->n_hierarchy_cdm = size_n_l_indexm (ppv->n_max_cdm, ppv->l_max_cdm, ppt2->m, ppt2->m_size);
    ppv->index_pt2_monopole_cdm = index_pt;
    index_pt += ppv->n_hierarchy_cdm;
    
    if (ppt2->perturbations2_verbose > 4)
      printf("     * cold dark matter hierarchy: we shall evolve %d equations.\n",
        ppv->n_hierarchy_cdm);
  }
   

  // ------------------------------------------------------------------------------------
  // -                             Metric perturbations                                 -
  // ------------------------------------------------------------------------------------

  /* Remember that here should go only the quantitites to be integrated, not
  those obeying constraint equations */

  /* Newtonian gauge */
  if (ppt->gauge == newtonian) {

    if (ppr2->compute_m[0] == _TRUE_)
      ppv->index_pt2_phi = index_pt++;

    if (ppr2->compute_m[1] == _TRUE_)
      ppv->index_pt2_omega_m1 = index_pt++;

    if (ppr2->compute_m[2] == _TRUE_) {
      ppv->index_pt2_gamma_m2 = index_pt++;
      ppv->index_pt2_gamma_m2_prime = index_pt++;
    }
  }

  /* Synchronous gauge */
  if (ppt->gauge == synchronous) {

    if (ppr2->compute_m[0] == _TRUE_)
      ppv->index_pt2_eta = index_pt++;

  }

  /* Finally, the number of differential equations to solve */
  ppv->pt2_size = index_pt;

  if (ppt2->perturbations2_verbose > 3)
    printf("     * we shall evolve %d equations\n", ppv->pt2_size);


  /* Allocate vectors for storing the values of all the index_pt2 perturbations
  and their time-derivatives. The allocation for y is made with calloc, so that
  in the following we shall only need to specify the non-vanishing initial
  conditions. */
  class_calloc (ppv->y, ppv->pt2_size, sizeof(double), ppt2->error_message);
  class_alloc (ppv->dy, ppv->pt2_size*sizeof(double), ppt2->error_message);
  class_alloc (ppv->used_in_sources, ppv->pt2_size*sizeof(int), ppt2->error_message);

  /* Each time the evolver gets close to a time that is in ppt2->tau_sampling, it will
  interpolate the pv->y array at that exact time. To optimise this process, it is
  possible to specify a list of the perturbations to be interpolated; these should be
  the perturbations needed to build the line of sight sources. The list is the logical
  array ppv->used_in_sources. For simplicity, we take of all of the evolved perturbations.
  This is not a big deal, as interp_from_dif, the routine used in evolver_ndf15.c
  to interpolate pv->y, is very fast in its job (especially if we are not sampling many
  time values).  */
  for (int index_pt=0; index_pt < ppv->pt2_size; index_pt++)
    ppv->used_in_sources[index_pt] = _TRUE_;



  // ====================================================================================
  // =                         Primordial initial conditions                            =
  // ====================================================================================

  /* Get background quantities */
  class_call (background_at_tau(
                pba,
                tau, 
                pba->normal_info, 
                pba->inter_closeby,
                &(ppw2->last_index_back), 
                ppw2->pvecback),
    pba->error_message,
    ppt2->error_message);

  double a = ppw2->pvecback[pba->index_bg_a];
  double Y = log10(a/pba->a_eq);  

  /* If the argument old_approx is NULL, then it means that we are going to evolve a new
  wavemode (k1,k2,k3). This time-interval, therefore, starts at tau_ini and
  the initial conditions need to be set from inflation (or any other early universe
  mechanism) using the function perturb2_initial_conditions(). */
  if (old_approx == NULL) {

    if (ppt2->perturbations2_verbose > 3)
      fprintf(stdout,
        "     * setting primordial initial conditions at tau=%g, a=%g, y=%g\n",
        tau, a, Y);

    /* Check that current approximation scheme is consistent with primordial initial
    conditions, which are obtained in the deep radiation era assuming tight coupling
    and all relevant modes super-horizon */
    if (ppt2->tight_coupling_approximation != tca2_none) {
      class_test(new_approx[ppw2->index_ap2_tca] == (int)tca_off,
        ppt2->error_message,
        "initial conditions assume tight-coupling approximation turned on");
    }

    class_test (new_approx[ppw2->index_ap2_rsa] == (int)rsa_on,
      ppt2->error_message,
      "initial conditions assume radiation streaming approximation turned off");
      
    if (pba->has_ur == _TRUE_)
      class_test (new_approx[ppw2->index_ap2_ufa] == (int)ufa_on,
        ppt2->error_message,
        "initial conditions assume ultra-relativistic fluid approximation turned off");

    class_test (new_approx[ppw2->index_ap2_nra] == (int)nra_on,
      ppt2->error_message,
      "initial conditions assume no-radiation approximation is turned off");

    /* Let ppw2->pv points towards the perturb2_vector structure that we
    created and filled above */
    ppw2->pv = ppv;

    /* Fill the vector ppw2->pv->y with appropriate initial conditions */
    class_call (perturb2_initial_conditions(
                  ppr,
                  ppr2,
                  pba,
                  pth,
                  ppt,
                  ppt2,
                  tau,
                  ppw2),
      ppt2->error_message,
      ppt2->error_message);
        
  } // end of if (old_approx == NULL)
    

  // ====================================================================================
  // =                       Approximations initial conditions                          =
  // ====================================================================================    

  /* Connect the last time-step of the previous time interval with the first time
  step of the current one.  IMPORTANT: In order to avoid segmentation faults and
  unpredictable behaviour, make sure that ppv->y (the new vector of perturbations)
  is addressed with ppv indices and that ppw2->pv->y (the vector of perturbations
  from the previous time interval) is addressed with ppw2->pv indices. */

  else {

    class_test((old_approx[ppw2->index_ap2_tca] == (int)tca_off)
      && (new_approx[ppw2->index_ap2_tca] == (int)tca_on),
      ppt2->error_message,
      "at tau=%g: the tight-coupling approximation can be switched off, not on",tau);

    /* Shortcuts to the old and new vectors of evolved perturbations */
    double * y_old = ppw2->pv->y;
    double * y_new = ppv->y;

    /* For those perturbations that are not affected by any approximation (metric,
    baryons, cdm, fluid...), we just copy their last known value (in ppt2->pv->y)
    in the new state vector (ppv->y). We treat such variables here. */

    /* Baryons */
    for (int n=0; n <= ppv->n_max_b; ++n) {
      for (int l=0; l <= ppv->l_max_b; ++l) {
      
        if ((l!=n) && (l!=0)) continue;
        if ((l==0) && (n%2!=0)) continue;
      
        for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
          
          int m = ppr2->m[index_m];

          y_new[ppv->index_pt2_monopole_b + nlm(n,l,m)] =
            y_old[ppw2->pv->index_pt2_monopole_b + nlm(n,l,m)];
        }
      }
    }

    /* Cold dark matter */
    if (pba->has_cdm == _TRUE_) {

      for (int n=0; n <= ppv->n_max_cdm; ++n) {
        for (int l=0; l <= ppv->l_max_cdm; ++l) {
      
          if ((l!=n) && (l!=0)) continue;
          if ((l==0) && (n%2!=0)) continue;
      
          for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
            
            int m = ppr2->m[index_m];

            y_new[ppv->index_pt2_monopole_cdm + nlm(n,l,m)] =
              y_old[ppw2->pv->index_pt2_monopole_cdm + nlm(n,l,m)];
          }
        }
      }
    }
    
    //  *** magnetic field
    
  	if (ppt2->has_magnetic_field == _TRUE_) {
      for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {    
      
        int m = ppr2->m[index_m];
			
				ppv->y[ppv->index_pt2_monopole_mag + lm(1,m)] =
            ppw2->pv->y[ppw2->pv->index_pt2_monopole_mag + lm(1,m)];          
             
      }
    }
    
    // *** Fluid component ***

    if (pba->has_fld == _TRUE_) { 
    }
      
    
    
    // *** Metric variables ***

    /* Synchronous gauge metric variables */
    if (ppt->gauge == synchronous) {
      
      if (ppr2->compute_m[0] == _TRUE_)
        y_new[ppv->index_pt2_eta] = y_old[ppw2->pv->index_pt2_eta];

    }
    
    /* Newtonian gauge metric variables */
    else if (ppt->gauge == newtonian) {

      if (ppr2->compute_m[0] == _TRUE_)
        y_new[ppv->index_pt2_phi] = y_old[ppw2->pv->index_pt2_phi];
      
      if (ppr2->compute_m[1] == _TRUE_)
        y_new[ppv->index_pt2_omega_m1] = y_old[ppw2->pv->index_pt2_omega_m1];

      if (ppr2->compute_m[2] == _TRUE_) {
        y_new[ppv->index_pt2_gamma_m2] = y_old[ppw2->pv->index_pt2_gamma_m2];
        y_new[ppv->index_pt2_gamma_m2_prime] = y_old[ppw2->pv->index_pt2_gamma_m2_prime];
      }
    }
    

    // ==================================================================================
    // =                                  TCA approximation                             =
    // ==================================================================================

    if ((old_approx[ppw2->index_ap2_tca] == (int)tca_on) && (new_approx[ppw2->index_ap2_tca] == (int)tca_off)) {

      /* Propagate the intensity dipole using the TCA relations */
      for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
        int m = ppt2->m[index_m];
        y_new[ppv->index_pt2_monopole_g + lm(1,m)] = ppw2->I_1m_tca1[m]; /* Intensity quadrupole */
      }

      /* Propagate the quadrupoles using the TCA relations */
      for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
        int m = ppt2->m[index_m];

        y_new[ppv->index_pt2_monopole_g + lm(2,m)] = ppw2->I_2m_tca1[m]; /* Intensity quadrupole */

        if (ppt2->has_polarization2 == _TRUE_) {
          y_new[ppv->index_pt2_monopole_E + lm(2,m)] = ppw2->E_2m_tca1[m]; /* E-modes quadrupole */
          y_new[ppv->index_pt2_monopole_B + lm(2,m)] = ppw2->B_2m_tca1[m]; /* B-modes quadrupole */
        }
      }
      
      /* Temperature hierarchy */
      for (int l=0; l<=ppw2->pv->l_max_g; ++l)
        for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m)
          y_new[ppv->index_pt2_monopole_g + lm(l,ppt2->m[index_m])]
            = y_old[ppw2->pv->index_pt2_monopole_g + lm(l,ppt2->m[index_m])];

      /* Polarization hierarchies */
      if (ppt2->has_polarization2 == _TRUE_) {

        for (int l=0; l<=ppw2->pv->l_max_pol_g; ++l)
          for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m)
            y_new[ppv->index_pt2_monopole_E + lm(l,ppt2->m[index_m])]
              = y_old[ppw2->pv->index_pt2_monopole_E + lm(l,ppt2->m[index_m])];

        for (int l=0; l<=ppw2->pv->l_max_pol_g; ++l)
          for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m)
            y_new[ppv->index_pt2_monopole_B + lm(l,ppt2->m[index_m])]
              = y_old[ppw2->pv->index_pt2_monopole_B + lm(l,ppt2->m[index_m])];
      }

      /* Neutrino hierarchy */
      if (pba->has_ur == _TRUE_)
        for (int l=0; l<=ppv->l_max_ur; ++l)
          for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m)
            y_new[ppv->index_pt2_monopole_ur + lm(l,ppt2->m[index_m])]
              = y_old[ppw2->pv->index_pt2_monopole_ur + lm(l,ppt2->m[index_m])];
      
    } // end of if(tca_on)



    // ==================================================================================
    // =                                  NRA approximation                             =
    // ==================================================================================
     
    if ((old_approx[ppw2->index_ap2_nra]==(int)nra_off) && (new_approx[ppw2->index_ap2_nra]==(int)nra_on)) {

      if (ppt2->perturbations2_verbose > 3)
        printf("     * switching on no-radiation approximation at tau=%g with a/a_eq=%g\n",
          tau, a/pba->a_eq);
      
      /* In the full NRA we do not evolve radiation at all.  Hence, we do not set initial
      conditions for photons and neutrinos because there are no equations for them */ 
      if (ppt2->no_radiation_approximation == (int)nra2_all ) {
        
      }

      /* In the fluid NRA, we evolve only the monopole and dipole. This fact is already
      encoded in ppv->l_max=1, which we set above */
      else if (ppt2->no_radiation_approximation == (int)nra2_fluid) {

        /* Temperature hierarchy */
        for (int l=0; l<=ppv->l_max_g; ++l)
          for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m)
            y_new[ppv->index_pt2_monopole_g + lm(l,ppt2->m[index_m])]
              = y_old[ppw2->pv->index_pt2_monopole_g + lm(l,ppt2->m[index_m])];

        /* Polarization hierarchies */
        if (ppt2->has_polarization2 == _TRUE_) {

          for (int l=0; l<=ppv->l_max_pol_g; ++l)
            for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m)
              y_new[ppv->index_pt2_monopole_E + lm(l,ppt2->m[index_m])]
                = y_old[ppw2->pv->index_pt2_monopole_E + lm(l,ppt2->m[index_m])];

          for (int l=0; l<=ppv->l_max_pol_g; ++l)
            for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m)
              y_new[ppv->index_pt2_monopole_B + lm(l,ppt2->m[index_m])]
                = y_old[ppw2->pv->index_pt2_monopole_B + lm(l,ppt2->m[index_m])];
        }

        /* Neutrino hierarchy */
        if (pba->has_ur == _TRUE_)
          for (int l=0; l<=ppv->l_max_ur; ++l)
            for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m)
              y_new[ppv->index_pt2_monopole_ur + lm(l,ppt2->m[index_m])]
                = y_old[ppw2->pv->index_pt2_monopole_ur + lm(l,ppt2->m[index_m])];
        
      } // end of if(nra_fluid)
    } // end of if(nra_on)


    /* Free the previous vector of perturbations */
    class_call (perturb2_vector_free(ppw2->pv),
      ppt2->error_message,
      ppt2->error_message);

    /* Let ppw2->pv point towards the perturb2_vector structure that we created
    and filled above */
    ppw2->pv = ppv;

  } // end of if(pba_old != NULL)

  /* Debug - print the initial conditions for the considered interval */
  // fprintf (stderr, "%20.10g\n", tau);
  // 
  // for (int index_pt=0; index_pt < ppw2->pv->pt2_size; ++index_pt)
  //   fprintf(stderr, "%20.10g\n", y_old[index_pt]);

  return _SUCCESS_;

}



/**
 * Free the perturb_vector structure.
 */

int perturb2_vector_free(
      struct perturb2_vector * pv
      )
{

  free(pv->y);
  free(pv->dy);
  free(pv->used_in_sources);
  free(pv);
  
  return _SUCCESS_;
}







int perturb2_free(
     struct precision2 * ppr2,
     struct perturbs2 * ppt2
     )
{

  /* Do not free memory if SONG was run only partially for debug purposes */
  if (ppt2->has_early_transfers1_only == _FALSE_)
    return _SUCCESS_;

  if (ppt2->has_perturbations2 == _TRUE_) {
    
    int k1_size = ppt2->k_size;

    /* Free the k1 level only if we are neither loading nor storing the sources to disk.  The memory
    management in those two cases is handled separately, so that the sources are freed as soon
    as they are not needed anymore via the source_load and sources_store functions. */
    if ((ppr2->store_sources_to_disk==_FALSE_) && (ppr2->load_sources_from_disk==_FALSE_)) {
      for (int index_k1 = 0; index_k1 < k1_size; ++index_k1) {

        /* If the transfer2 module was loaded, ppt2->sources had already been freed  */
        if (ppt2->has_allocated_sources[index_k1] == _TRUE_)
          class_call(perturb2_free_k1_level(ppt2, index_k1), ppt2->error_message, ppt2->error_message); 
      }
    }

    free (ppt2->has_allocated_sources);

    for (int index_type = 0; index_type < ppt2->tp2_size; index_type++) {
        
      free(ppt2->sources[index_type]);
      free(ppt2->tp2_labels[index_type]);

    } // End of for index_type


    for (int index_k1 = 0; index_k1 < k1_size; ++index_k1) {
      for (int index_k2 = 0; index_k2 <= index_k1; ++index_k2)
        free (ppt2->k3[index_k1][index_k2]);

      free (ppt2->k3[index_k1]);
      free (ppt2->k3_size[index_k1]);
      if (ppt2->k3_sampling == smart_k3_sampling)
        free (ppt2->index_k3_min[index_k1]);

    }

    free(ppt2->sources);
    
    free(ppt2->tau_sampling);
           
    free(ppt2->k);
    free(ppt2->k3);
    free(ppt2->k3_size);
    if (ppt2->k3_sampling == smart_k3_sampling)
      free(ppt2->index_k3_min);
    
    free(ppt2->tp2_labels);

    /* Free memory for the general coupling coefficients */  
    int m1_min = -ppt2->l1_max;
    int m2_min = -ppt2->l2_max;
    int m1_max = ppt2->l1_max;
    int m2_max = ppt2->l2_max;
    int l1_size = ppt2->l1_max+1;
    int l2_size = ppt2->l2_max+1;
    int m1_size = m1_max-m1_min+1;
    int m2_size = m2_max-m2_min+1;
    
    for (int index_pf=0; index_pf < ppt2->pf_size; ++index_pf) {
      for (int l=0; l <= ppt2->largest_l; ++l) {
        for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
          int m = ppr2->m[index_m];
          for (int l1=0; l1 <= ppt2->l1_max; ++l1) {  
            for (int m1=m1_min; m1 <= m1_max; ++m1) {
              free (ppt2->coupling_coefficients[index_pf][lm(l,m)][l1][m1-m1_min]);
            } // end of for m1
            free (ppt2->coupling_coefficients[index_pf][lm(l,m)][l1]);
          } // end of for l1
          free (ppt2->coupling_coefficients[index_pf][lm(l,m)]);
        } // end of for m
      } // end of for l
    free (ppt2->coupling_coefficients[index_pf]);
    } // end of for T,E,B...
    free (ppt2->coupling_coefficients);

    free(ppt2->m);
    free(ppt2->corresponding_l);
    free(ppt2->corresponding_index_m);
    free(ppt2->index_monopole);

    for (int l=0; l <= ppt2->largest_l; ++l)
      free(ppt2->lm_array[l]);
    free(ppt2->lm_array);
    
    for (int l=0; l <= ppt2->largest_l_quad; ++l)
      free(ppt2->lm_array_quad[l]);
    free(ppt2->lm_array_quad);
    
    for (int n=0; n <= 2; ++n) {
      for (int l=0; l <= 2; ++l) {
        if ((l!=n) && (l!=0)) continue;
        if ((l==0) && (n%2!=0)) continue;
        free(ppt2->nlm_array[n][l]); 
      }
      free(ppt2->nlm_array[n]);
    }
    free(ppt2->nlm_array);

    /* Free coupling coefficients */
    int n_multipoles = size_l_indexm (ppt2->largest_l, ppt2->m, ppt2->m_size);

    for (int i=0; i < n_multipoles; ++i) {
      free (ppt2->c_minus[i]);
      free (ppt2->c_plus[i]);
      free (ppt2->d_minus[i]);
      free (ppt2->d_plus[i]);
      free (ppt2->d_zero[i]);
    }

    free (ppt2->c_minus);
    free (ppt2->c_plus);
    free (ppt2->d_minus);
    free (ppt2->d_plus);
    free (ppt2->d_zero);

    /* Free file arrays */
    if ((ppr2->store_sources_to_disk == _TRUE_) || (ppr2->load_sources_from_disk == _TRUE_)) {
    
      // fclose(ppt2->sources_status_file);
    
      for (int index_k1=0; index_k1<ppt2->k_size; ++index_k1)
        free (ppt2->sources_paths[index_k1]);
    
      free (ppt2->sources_files);
      free (ppt2->sources_paths);
    
    }

    /* Close the debug files */
    if(ppt2->has_debug_files) {
      fclose(ppt2->transfers_file);
      fclose(ppt2->quadsources_file);
      fclose(ppt2->quadliouville_file);
      fclose(ppt2->quadcollision_file);
    }

  } // end of if(has_perturbations2)

  return _SUCCESS_;

}



/**
 * Compute the time derivatives of all the perturbations to be integrated.
 *
 * The derivatives of the perturbations are with respect to conformal time (tau)
 * and are computed using as an input the current value of the perturbations, contained
 * in the argument y. You can find find a summary of all equations involved in sec. 5.3.1 
 * http://arxiv.org/abs/1405.2280. More technical details on the differential system
 * and on the solver can be found in sec 5.3.2 and 5.3.3.
 *
 * We are going to store the time derivatives in the array dy, passed as argument.
 * Both y and dy are accessed with the index_pt2_XXX indices.
 *
 * This function is never called explicitly in this module. Instead, it is passed as an
 * argument to the evolver, which calls it whenever it needs to update dy. Since the
 * evolver should work with functions passed from various modules, the format of the
 * arguments is a bit special:
 * - fixed parameters and workspaces are passed through the generic pointer.
 *   parameters_and_workspace.
 * - errors are not written as usual in ppt2->error_message, but in a generic
 *   error_message passed in the list of arguments.
 */

int perturb2_derivs (
      double tau, /**< current time */
      double * y, /**< values of evolved perturbations at tau (y[index_pt2_XXX]) */
      double * dy, /**< output: derivatives of evolved perturbations at tau (dy[index_pt2_XXX]) */
      void * parameters_and_workspace, /**< generic structure with all needed parameters, including
                                       backgroudn and thermo structures; will be cast to type
                                       perturb2_parameters_and_workspace()  */
      ErrorMsg error_message /**< error message */
      )
{
  
  /* Define shortcuts to avoid heavy notation */
  struct perturb2_parameters_and_workspace * pppaw2 = parameters_and_workspace;
  struct precision * ppr = pppaw2->ppr;
  struct precision2 * ppr2 = pppaw2->ppr2;
  struct background * pba = pppaw2->pba;
  struct thermo * pth = pppaw2->pth;
  struct perturbs * ppt = pppaw2->ppt;
  struct perturbs2 * ppt2 = pppaw2->ppt2;
  struct perturb2_workspace * ppw2 = pppaw2->ppw2;
  double * pvecback = ppw2->pvecback;
  double * pvecthermo = ppw2->pvecthermo;
  double * pvecmetric = ppw2->pvecmetric;
  double k_sq = ppw2->k_sq;
  double k = ppw2->k;

  /* Update counter of calls */
  ppw2->derivs_count++;
  
  
  
  // ======================================================================================
  // =                          Interpolate needed quantities                             =
  // ======================================================================================

  /* Interpolate background-related quantities (pvecback) */
  class_call (background_at_tau (
                pba,
                tau, 
                pba->normal_info, 
                pba->inter_closeby,
                &(ppw2->last_index_back), 
                ppw2->pvecback),
    pba->error_message,
    error_message);

  double a = pvecback[pba->index_bg_a];
  double Hc = pvecback[pba->index_bg_H] * a;
  double z = 1./a-1.;


  /* Interpolate thermodynamics-related quantities (pvecthermo) */
  class_call (thermodynamics_at_z (
                pba,
                pth,
                1./pvecback[pba->index_bg_a]-1.,  /* redshift z=1./a-1 */
                pth->inter_closeby,
                &(ppw2->last_index_thermo),
                ppw2->pvecback,
                ppw2->pvecthermo),
    pth->error_message,
    error_message);

  double r = pvecback[pba->index_bg_rho_g]/pvecback[pba->index_bg_rho_b];
  double kappa_dot = pvecthermo[pth->index_th_dkappa];


  /* Interpolate quadratic sources from the precomputed table. This is quick
  but might lead to some numerical noise when the perturbations are very small.
  Comment and use perturb2_quadratic_sources() below instead to reduce the noise
  at the expense of speed. Note that when using interpolation the quadratic
  sources of the collision term (pvec_quadcollision) will not be available. */
  if (ppt2->has_quadratic_sources == _TRUE_) {
    class_call (perturb2_quadratic_sources_at_tau(
                 ppr,
                 ppr2,
                 ppt,
                 ppt2,
                 tau,
                 interpolate_total,
                 ppw2
                 ),
      ppt2->error_message,
      error_message);
  }

  /* Uncomment to compute the quadratic sources directly without using
  interpolatoin. Good to reduce the numerical noise, but it is likely to
  slightly increase the computation time. It also computes the quadratic
  sources for the collision term. */
  // if (ppt2->has_quadratic_sources == _TRUE_) {
  //   class_call (perturb2_quadratic_sources(
  //                 ppr,
  //                 ppr2,
  //                 pba,
  //                 pth,
  //                 ppt,
  //                 ppt2,
  //                 -1,
  //                 tau,
  //                 compute_total_and_collision,
  //                 ppw2->pvec_quadsources,
  //                 ppw2->pvec_quadcollision,
  //                 ppw2
  //                 ),
  //     ppt2->error_message,
  //     error_message);
  // }
  
  /* Set the perturbations that are influenced by approximations */
  class_call (perturb2_workspace_at_tau(
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2,
                tau,
                y,
                ppw2),
    ppt2->error_message,
    ppt2->error_message);
  
  /* Interpolate Einstein equations and store them in pvec_metric. Note that
  perturb2_einstein relies on ppw2->pvecback, ppw2->pvecthermo, and
  ppw2->pvec_quadsources as calculated above.  Therefore, it should always be
  called after the functions background_at_tau(), thermodynamics_at_z(), and
  perturb2_quadratic_sources(). */
  class_call (perturb2_einstein(
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2,
                tau,
                y,
                ppw2), /* will fill ppw2->pvecmetric */
    ppt2->error_message,
    error_message);


  /* Assign shortcuts for metric variables */

  double phi_prime=0, psi=0, phi=0, omega_m1=0, omega_m1_prime=0, gamma_m2_prime=0;

  if (ppt->gauge == newtonian) {

    /* Scalar potentials */
    if (ppr2->compute_m[0] == _TRUE_) {
      phi_prime = pvecmetric[ppw2->index_mt2_phi_prime];
      phi = y[ppw2->pv->index_pt2_phi];
      psi = pvecmetric[ppw2->index_mt2_psi];
    }

    /* Vector potentials */
    if (ppr2->compute_m[1] == _TRUE_) {
      omega_m1 = y[ppw2->pv->index_pt2_omega_m1];
      omega_m1_prime = pvecmetric[ppw2->index_mt2_omega_m1_prime];
    }
    
    /* Tensor potentials */
    if (ppr2->compute_m[2] == _TRUE_)
      gamma_m2_prime = y[ppw2->pv->index_pt2_gamma_m2_prime];

  } // end of if newtonian


  /* Split each equations for CDM and baryons into a metric (gauge dependenent) part
  and a matter (gauge independent) part.  The procedure is the same as in the first-
  order case.  Note, however, that here (1) metric_euler is different, because now we are
  integrating 'v_0' rather than theta, and (2) the metric term is present only for the 
  scalar m=0 equations. */
  double metric_continuity=0, metric_euler=0, metric_shear=0, metric_shear_prime=0;

  if (ppt->gauge == synchronous) {

    // metric_continuity = 0.5*pvecmetric[ppw2->index_mt2_h_prime];
    // metric_euler = 0.;
    // metric_shear = (pvecmetric[ppw2->index_mt2_h_prime] + 6. * pvecmetric[ppw2->index_mt2_eta_prime])/2.;
  }
  else if (ppt->gauge == newtonian) {

    if (ppr2->compute_m[0] == _TRUE_) {
      metric_continuity = -3.*phi_prime;
      metric_euler = k*psi;  
      metric_shear = 0.;
    }
  }

  // ===========================================================================================
  // =                                    Boltzmann hierarchies                                =
  // ===========================================================================================

  int l_max_g = ppw2->pv->l_max_g;
  int l_max_pol_g = ppw2->pv->l_max_pol_g;
  int l_max_ur = ppw2->pv->l_max_ur;
  int l_max_b = ppw2->pv->l_max_b;
  int l_max_cdm = ppw2->pv->l_max_cdm;

  double * I_1m = &(ppw2->I_1m[0]);
  double * I_2m = &(ppw2->I_2m[0]);
  double * E_2m = &(ppw2->E_2m[0]);
  double * B_2m = &(ppw2->B_2m[0]);
  double * b_22m = &(ppw2->b_22m[0]);
  double b_200 = ppw2->b_200;


  // -----------------------------------------------------------------------------------------
  // -                                  Photon temperature                                   -
  // -----------------------------------------------------------------------------------------

  /* - Monopole equation */

  if (l_max_g > -1) {
    if (ppr2->compute_m[0] == _TRUE_) {
      dI(0,0) = -four_thirds * (k*I_1m[0]/4 + metric_continuity);
    }
  }
  

  /* - Dipole equation */

  if (l_max_g > 0) {
    for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {

      int m = ppt2->m[index_m];

      /* The free streaming term is k*(I(0,0)-2/5*I(2,0)) for m=0 and
      -k*sqrt(3)/5*I(2,1) for m=1. The collision term is
      kappa_dot*(4/3*b_11m-I_1m). */
      dI(1,m) = k * (c_minus(1,m,m)*I(0,m) - c_plus(1,m,m)*I_2m[m])
                + ppw2->C_1m[m];
      
      /* Scalar metric contribution */
      if (m==0)
        dI(1,m) += 4*metric_euler;

      /* Vector metric contribution */
      if (m==1)
        dI(1,m) += - 4*omega_m1_prime;
    }
  }


  /* - Quadrupole equation */

  if (l_max_g > 1) {

    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {

      int m = ppt2->m[index_m];
    
      /* To recover Ma & Berty's notation (same as CLASS), consider that for photons
      I_20 = 10 * shear_g and E_20 = -5/sqrt(6) * (G_0 + G_2) */
      double Pi = 0.1 * ( I_2m[m] - sqrt_6*E_2m[m] );

      /* The free streaming term is k*(2/3*I(1,0)-3/7*I(3,0)) for m=0 and
      k*(sqrt(3)/3*I(1,1)-sqrt(8)/7*I(3,1)) for m=1 */
      dI(2,m) = k * (c_minus(2,m,m)*I_1m[m] - c_plus(2,m,m)*I(3,m))
                - kappa_dot*(I_2m[m] - Pi);
    
      /* Tensor metric contribution */
      /* Note that this contribution has an opposite sign with respect to the vector one,
      -4*omega_m1_prime. This happens because the spherical decomposition of n^i n^j gamma_ij
      is equal to -gamma[m] while that of n^i omega_i is equal to +omega[m]. */
      if (m==2)
        dI(2,m) += 4 * gamma_m2_prime;
    }
  }


  /* - l>2 moments */

  /* Higher moments are all tight coupling suppressed. Note that c_minus is always
  positive, while c_plus is always negative (at least in this case where m1==m). */
  for (int l=3; l<=l_max_g; ++l) {
    for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
      
      int m = ppt2->m[index_m];
      
      dI(l,m) = k * (c_minus(l,m,m)*I(l-1,m) - c_plus(l,m,m)*I(l+1,m))
                - kappa_dot * I(l,m);
      
    }
  }


  /* - Closure relation */

  /* For the last multipole, we evolve a different equation to avoid numerical
  reflection. We take this closure relation from eq. D.1 of Pitrou et al. 2010. */

  if (ppw2->pv->use_closure_g == _TRUE_) {
    int l = l_max_g;
    for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
      int m = ppt2->m[index_m];

      double c_pre =  (m==0 ?  (2*l+1)/(2*l-1.) : sqrt((double)(l + abs(m))/(l - abs(m))) * (2*l+1)/(2*l-1.) );
      double c     =  (l+1+abs(m))/(k*tau);

      dI(l,m) = k*(c_pre*I(l-1,m) - c*I(l,m)) - kappa_dot*I(l,m);
    }
  }


  /* - Add quadratic terms */

  if (ppt2->has_quadratic_sources == _TRUE_)
    for (int l=0; l<=MIN(ppr2->l_max_g_quadsources,l_max_g); ++l)
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m)
        dI(l,ppr2->m[index_m]) += dI_qs2(l,ppr2->m[index_m]);



  // -----------------------------------------------------------------------------------------
  // -                                     Polarisation                                      -
  // -----------------------------------------------------------------------------------------

  if (ppt2->has_polarization2 == _TRUE_) {
  
    /* Set the monopole and dipole to zero. The hierarchies for the E-modes and B-modes
    start from the quadrupole, we keep the monopole and dipole to keep the structure
    of SONG simpler. */  
    if (l_max_pol_g > -1)
      if (ppr2->compute_m[0] == _TRUE_)
        dE(0,0) = dB(0,0) = 0;
  
    if (l_max_pol_g > 0) {
      for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
        int m = ppr2->m[index_m];
        dE(1,m) = dB(1,m) = 0;
      }
    }


    /* - E-modes quadrupole */

    /* For the E-modes, we use eq. 4.146 of http://arxiv.org/abs/1405.2280, which is based
    on the first two lines of eq. 2.19 of Beneke, Fidler & Klingmuller (2011), hereafter
    BFK */

    if (l_max_pol_g > 1) {
      for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
        int m = ppt2->m[index_m];
        double Pi = 0.1 * ( I_2m[m] - sqrt_6*E_2m[m] );
        dE(2,m) = k * ( -d_plus(2,m,m)*E(3,m) - d_zero(2,m,m)*B_2m[m] )
                  - kappa_dot*(E_2m[m] + sqrt_6*Pi);
      }
    }


    /* - E-modes higher moments */
    
    for (int l=3; l<=l_max_pol_g; ++l) {
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
        int m = ppt2->m[index_m];
        dE(l,m) = k*(d_minus(l,m,m)*E(l-1,m) - d_plus(l,m,m)*E(l+1,m)
                  - d_zero(l,m,m)*B(l,m))  /* Mixing between E- and B-modes */
                  - kappa_dot*E(l,m);
      }
    }
    

    /* - B-modes moments */

    /* For the B-modes, we use the equation in the first line of eq. 2.20 in BFK.
    Note that the B-modes are always zero for m=0, and that they only couple
    to E-modes. */

    for (int l=2; l<=l_max_pol_g; ++l) {
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
        int m = ppt2->m[index_m];
        dB(l,m) = k*(d_minus(l,m,m)*B(l-1,m) - d_plus(l,m,m)*B(l+1,m)
                  + d_zero(l,m,m)*E(l,m)) /* Mixing between E- and B-modes */
                  - kappa_dot*B(l,m);
      }
    }


    /* - Closure relations */

    /* For the last multipole, we evolve a different equation to avoid numerical
    reflection. We take this closure relation from the real & imaginary parts of
    eq. D.2 of Pitrou et al. (2010), hereafter P2010. */

    if (ppw2->pv->use_closure_pol_g == _TRUE_) {
      int l = l_max_pol_g;
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
        int m = ppt2->m[index_m];

        double c_pre =  sqrt(1-(((double)m)*m)/(l*l) * (l+2)/(l-2.)) * (2*l+1)/(2*l-1.);
        double c     =  (l+3)/(k*tau);
        double c_mix =  ((double)m)/((double)l);

        dE(l,m) = k*( c_pre*E(l-1,m) - c*E(l,m) - c_mix*B(l,m) ) - kappa_dot*E(l,m);
        dB(l,m) = k*( c_pre*B(l-1,m) - c*B(l,m) + c_mix*E(l,m) ) - kappa_dot*B(l,m);
      }
    } // end of if(l_max_pol_g == ppw2->l_max_pol_g)


    /* - Add quadratic terms */
    
    if (ppt2->has_quadratic_sources == _TRUE_) {
      for (int l=2; l<=MIN(ppr2->l_max_pol_g_quadsources,l_max_pol_g); ++l) {
        for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

          int m = ppt2->m[index_m];

          dE(l,m) += dE_qs2(l,m);
          dB(l,m) += dB_qs2(l,m);                

        } // end of for (m)
      } // end of for (l)
    } // end of if(has_quadratic_sources)

  }  // end of if(has_polarization2)

      
      
  // -----------------------------------------------------------------------------------------
  // -                                     Neutrinos                                         -
  // -----------------------------------------------------------------------------------------

  /* The neutrinos obey the same Boltzmann hierarchy as the photons' one, with kappa_dot 
  set to zero. */

  if (pba->has_ur == _TRUE_) {

    /* - Monopole */
    
    if (l_max_ur > -1)
      if (ppr2->compute_m[0] == _TRUE_)
        dN(0,0) = -four_thirds * (k*N(1,0)*0.25 + metric_continuity);
  

    /* - Dipole */
    
    if (l_max_ur > 0) {
      
      for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    
        int m = ppt2->m[index_m];
    
        dN(1,m) = k * (c_minus(1,m,m)*N(0,m) - c_plus(1,m,m)*N(2,m));

        /* Scalar metric contribution */
        if (m==0)
          dN(1,m) += 4*metric_euler;

        /* Vector metric contribution */
        if (m==1)
          dN(1,m) += - 4*omega_m1_prime;
      }
    }

    /* - Higher moments */

    for (int l=2; l<=l_max_ur; ++l) {
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
      
        int m = ppt2->m[index_m];
      
        dN(l,m) = k * (c_minus(l,m,m)*N(l-1,m) - c_plus(l,m,m)*N(l+1,m));
      
      }
    }


    /* - Tensor metric contribution */
    
    if (l_max_ur > 1)
      if (ppr2->compute_m[2] == _TRUE_)
        dN(2,2) += 4 * gamma_m2_prime;


    /* - Closure relation */

    /* For the last multipole, we evolve a different equation to avoid numerical
    reflection. We take this closure relation from eq. D.1 of Pitrou et al. 2010
    without the kappa_dot term. */

    if (ppw2->pv->use_closure_ur == _TRUE_) {
      int l = l_max_ur;
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
        int m = ppt2->m[index_m];
  
        double c_pre =  (m==0 ?  (2*l+1)/(2*l-1.) : sqrt((double)(l + abs(m))/(l - abs(m))) * (2*l+1)/(2*l-1.) );
        double c     =  (l+1+abs(m))/(k*tau);
  
        dN(l,m) = k*(c_pre*N(l-1,m) - c*N(l,m));
      }
    } // end of if(l_max_ur == ppw2->l_max_ur)
    

    /* - Add quadratic terms */

    if (ppt2->has_quadratic_sources == _TRUE_)
      for (int l=0; l<=MIN(ppr2->l_max_ur_quadsources,l_max_ur); ++l)
        for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m)
          dN(l,ppr2->m[index_m]) += dN_qs2(l,ppr2->m[index_m]);  
  
  }  // end of if(has_ur)


  // -----------------------------------------------------------------------------------------
  // -                                       Baryons                                         -
  // -----------------------------------------------------------------------------------------

  /* - Baryon monopole */
  
  if (l_max_b > -1)
    if (ppr2->compute_m[0] == _TRUE_)
      db(0,0,0) = - (k*b(1,1,0)*one_third + metric_continuity) - Hc*b_200;

  
  /* - Baryon dipole */

  /* The baryon collision term for the monopole and dipole is the same as the photon's,
  multiplied by -r = -rho_g/rho_b. This can be seen by enforcing the conservation of
  energy and momentum, expressed through the energy momentum tensor:
  
    d T00_b / d tau = - d T00_g / d tau ,
    d T0i_b / d tau = - d T0i_g / d tau .       
  
  The time derivatives here account only for the variations due to the Thomson
  scattering, which are very localised in time compared to the gravitational
  interaction. Therefore in terms of the energy momentum tensor, the baryon and
  photon collision terms are equal and opposite.

  We describe the distribution function of baryons through its expansion in beta-moments,
  which are conceptually the same as the photon multipoles. We do so because we want to
  treat baryons and photons on the same ground. One of the advantages of this approach
  is manifest here.  Since the first two beta-moments are just T00/rho (monopole) and
  T0i/rho (dipole), we have that the collision term for the first two baryon moments
  is the same as the photon's, times -r = -rho_g/rho_b. Had we used fluid variables instead
  (delta and velocity), there would have been a -R = -4/3 rho_g/rho_b factor instead, and
  two additional terms quadratic in the photon and baryon velocities. */

  if (l_max_b > 0) {
    
    for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    
      int m = ppt2->m[index_m];
    
      /* With respect to the photon dipole, there is an extra damping term given by
      -Hc*dipole. The free streaming term is k*(b(0,0)-2/5*b(2,0)) for m=0 and
      -k*sqrt(3)/5*b(2,1) for m=1. The collision term is the same as the photon
      dipole's, multiplied by r=-rho_g/rho_b. This is eq. 5.16 of
      http://arxiv.org/abs/1405.2280. */
      db(1,1,m) = - Hc * b(1,1,m)
                  + k * (c_minus(1,m,m)*b_200 - c_plus(1,m,m)*b_22m[m])
                  - r * ppw2->C_1m[m];
    
      /* Scalar metric contribution */
      if (m==0)
        db(1,1,m) += 3 * metric_euler;

      /* Vector metric contribution - see Beneke and Fidler 2011 (eq. A.10) */
      if (m==1)
        db(1,1,m) += - 3 * (omega_m1_prime + Hc*omega_m1);
    }
  }



  /* - Baryon "pressure" */
  
  /* The (2,0,0) moment is exponentially suppressed in absence of quadratic sources */
  if (l_max_b > -1)
    if (ppr2->compute_m[0] == _TRUE_)
      if (ppt2->has_perfect_baryons == _FALSE_)
        db(2,0,0) = - 2*Hc*b(2,0,0);
  

  /* - Baryon quadrupole */
  
  /* The (2,2,m) moments are exponentially suppressed in absence of quadratic sources */
  if (l_max_b > 1)
    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m)
      if (ppt2->has_perfect_baryons == _FALSE_)
        db(2,2,ppt2->m[index_m]) = - 2*Hc*b(2,2,ppt2->m[index_m]);


  // *** Add baryon quadratic sources

  if (ppt2->has_quadratic_sources == _TRUE_) {
    for (int n=0; n <= ppw2->pv->n_max_b; ++n) {
      for (int l=0; l <= l_max_b; ++l) {
        if ((l!=n) && (l!=0)) continue;
        if ((l==0) && (n%2!=0)) continue;
        if (ppt2->has_perfect_baryons == _TRUE_)
          if ((n>1)||(l>1)) continue;
        for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
          int m = ppr2->m[index_m];
          db(n,l,m) += db_qs2(n,l,m);
        }
      }
    }
  }

  // ----------------------------------------------
  // -               magnetic fields              -
  // ----------------------------------------------
	if (ppt2->has_magnetic_field == _TRUE_) {
		
		dmag(0,0) = 0.;
		
		for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
      int m = ppt2->m[index_m];	
      
      /* the sources are hubble dilution and the tight coupling 
      suppressed differences of baryon and photon velocity
      
      The quadsources contain the quadratic part and the generation 
      from photon anisotropic stress*/
      
       
      dmag(1,m) = -2. * Hc * mag(1,m)  
      					+
      					k* pvecback[pba->index_bg_rho_g] /3. *ppw2->C_1m[m]/kappa_dot
      					/*(four_thirds * b(1,1,m) - ppw2->I_1m[m])*/ 
      					;
      					
      					 /*this is the conversion factor, after this add the collison term for photons stripped by kappa+_dot. Numerical factor of sigma_t/e missing*/
      					/*keep in mind that the collision term for photons does include the perturbation of xe form delta_b (and possible perturbed recombination) This needs to be removed for the magnetic field. Then an additional term of - psi has to be added seperately coming from the vierbein. Note that the contribution from phi is already included in the collision term*/		
      	 				
      	}
		
		if (ppt2->has_quadratic_sources == _TRUE_) {
      for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
        int m = ppr2->m[index_m];
        dmag(1,m) +=  dmag_qs2(1,m);
		}}
	}


  // -----------------------------------------------------------------------------------------
  // -                                    Cold Dark Matter                                   -
  // -----------------------------------------------------------------------------------------

  if (pba->has_cdm == _TRUE_) {

    /* - CDM monopole */
  
    if (l_max_cdm > -1)
      if (ppr2->compute_m[0] == _TRUE_)
        dcdm(0,0,0) = - (k*cdm(1,1,0)*one_third + metric_continuity) - Hc*ppw2->cdm_200;
    
  
    /* - CDM dipole */

    if (l_max_cdm > 0) {

      for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    
        int m = ppt2->m[index_m];
    
        dcdm(1,1,m) = - Hc * cdm(1,1,m)
                      + k * (c_minus(1,m,m)*ppw2->cdm_200 - c_plus(1,m,m)*ppw2->cdm_22m[m]);

        /* Scalar metric contribution */
        if (m==0)
          dcdm(1,1,m) += 3 * metric_euler;

        /* Vector metric contribution - see Beneke and Fidler 2011 (eq. A.10) */
        if (m==1)
          dcdm(1,1,m) += - 3 * (omega_m1_prime + Hc*omega_m1);
      }
    }


    /* - CDM "pressure" */

    /* The (2,0,0) moment is exponentially suppressed in absence of quadratic sources */
    if (l_max_cdm > -1)
      if (ppr2->compute_m[0] == _TRUE_)
        if (ppt2->has_perfect_cdm == _FALSE_)
          dcdm(2,0,0) = - 2*Hc*cdm(2,0,0);


    /* CDM quadrupole */

    /* The (2,2,m) moments are exponentially suppressed in absence of quadratic sources */
    /* TODO: Should we add here (and for baryons) the tensor potential? */
    if (l_max_cdm > 1)
      for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m)
        if (ppt2->has_perfect_cdm == _FALSE_)
          dcdm(2,2,ppt2->m[index_m]) = - 2*Hc*cdm(2,2,ppt2->m[index_m]);
      

    /* - Add CDM quadratic sources */
    
    if (ppt2->has_quadratic_sources == _TRUE_) {
      for (int n=0; n <= ppw2->pv->n_max_cdm; ++n) {
        for (int l=0; l <= l_max_cdm; ++l) {    
          if ((l!=n) && (l!=0)) continue;
          if ((l==0) && (n%2!=0)) continue;
          if (ppt2->has_perfect_cdm == _TRUE_)
            if ((n>1)||(l>1)) continue;
          for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
            int m = ppr2->m[index_m];
            dcdm(n,l,m) += dcdm_qs2(n,l,m);
          }
        }
      }
    }

  } // end of if(cdm)
  



  // ===============================================================================
  // =                               Metric Perturbations                          =
  // ===============================================================================

  /* Here we set the value of the time derivatives of the evolved metric variables to what
  was computed in perturb2_einstein(). We use a different function to do that because it is
  convenient to keep everything related to Einstein equations and the energy momentum
  tensor together. Note that here there is no need to add the quadratic sources since we
  already did that in perturb2_einstein(). */

  // **** Newtonian gauge
  if (ppt->gauge == newtonian) {

    /* Curvature potential phi */
    if (ppr2->compute_m[0] == _TRUE_)
      dy[ppw2->pv->index_pt2_phi] = pvecmetric[ppw2->index_mt2_phi_prime];
    
    /* Vector potential omega */
    if (ppr2->compute_m[1] == _TRUE_)
      dy[ppw2->pv->index_pt2_omega_m1] = pvecmetric[ppw2->index_mt2_omega_m1_prime];
    
    /* Tensor potential gamma */
    if (ppr2->compute_m[2] == _TRUE_) {
      dy[ppw2->pv->index_pt2_gamma_m2] = y[ppw2->pv->index_pt2_gamma_m2_prime];
      dy[ppw2->pv->index_pt2_gamma_m2_prime] = pvecmetric[ppw2->index_mt2_gamma_m2_prime_prime];
    }
    
  } // end of if newtonian



  // *** Synchronous gauge
  if (ppt->gauge == synchronous) {

    /* Curvature potential eta */
    if (ppr2->compute_m[0] == _TRUE_)
      dy[ppw2->pv->index_pt2_eta] = pvecmetric[ppw2->index_mt2_eta_prime];

  } // end of if synchronous

  // *** Print some debug information
  // if ( (1==0) || (ppw2->derivs_count == 793) || (ppw2->derivs_count == 794)) {
  //   if ((ppw2->index_k1==0) && (ppw2->index_k2==1)) {
  // 
  //     printf(" *** Leaving derivs for the %d time; tau = %.16f\n", ppw2->derivs_count, tau);
  // 
  //     /* Show content of y, dy and dy_quadsources */
  //     int index_pt;
  //     for (int index_pt=0; index_pt<ppw2->pv->pt2_size; ++index_pt)
  //       printf("y[%3d] = %+12.3g,  dy[%3d] = %+12.3g,   dy_quadsources[%3d] = %+12.3g\n",
  //         index_pt, y[index_pt], index_pt, dy[index_pt], index_pt, ppw2->pv->dy_quadsources[index_pt]);
  // 
  //   }  
  // }

  // *** Print some debug information as a function of time
  // if ((ppw2->index_k1==0) && (ppw2->index_k2==1)) {
  //   fprintf (stderr, "%17.7g %17.7g %17.7g\n", tau, y[ppw2->pv->index_pt2_phi], dy[ppw2->pv->index_pt2_phi]);
  // }

  return _SUCCESS_;
  
}   // end of perturb2_derivs





/**
 * Compute the metric quantities at a given time and fill the ppw2->pvecmetric
 * array.
 *
 * Before calling this function, remember to compute the quadratic sources using
 * perturb2_quadratic_sources().  The result of such computation is stored in the
 * array ppw2->pvec_quadsources, while the single first-order perturbations
 * at time tau are in ppw2->pvec_sources1 and ppw2->pvec_sources2.
 * This function also relies on ppw2->pvecback and ppw2->pvecthermo, which
 * should have already been filled.  This is done in perturb2_derivs().
 */
int perturb2_einstein (
       struct precision * ppr,
       struct precision2 * ppr2,
       struct background * pba,
       struct thermo * pth,
       struct perturbs * ppt,
       struct perturbs2 * ppt2,
       double tau,
       double * y,
       struct perturb2_workspace * ppw2
       )
{

  /* Contribution of the single species */
  double rho_g=0, rho_b=0, rho_cdm=0, rho_ur=0;
  
  /* Variables that will accumulate the contribution from all species */
  double rho_monopole=0, rho_dipole=0, rho_quadrupole=0,
         rho_dipole_m1=0, rho_quadrupole_m1=0,
         rho_quadrupole_m2;

  /* Geometrical and background quantities */
  double k_sq = ppw2->k_sq;
  double k = ppw2->k; 
  double a = ppw2->pvecback[pba->index_bg_a];
  double a_sq = a*a;
  double Hc = ppw2->pvecback[pba->index_bg_H]*a;
  double r = ppw2->pvecback[pba->index_bg_rho_g]/ppw2->pvecback[pba->index_bg_rho_b];
    
  
  // ==========================================================================================
  // =                                  Energy momentum tensor                                =
  // ==========================================================================================

  /* We shall compute the monopole, dipole and quadrupole of the distribution function
  of all species.  At first order these would be the energy density, velocity divergence
  and anisotropic stresses of the total fluid.  This interpretation does not hold at
  second order, where the fluid variables are related to the distribution function
  by terms quadratic in the first order velocities and densities */
    

  // *** Photon contribution.
  rho_g = ppw2->pvecback[pba->index_bg_rho_g];
  
  if (ppr2->compute_m[0] == _TRUE_) {
    rho_monopole           =  rho_g*I(0,0);
    rho_dipole             =  rho_g*ppw2->I_1m[0];
    rho_quadrupole         =  rho_g*ppw2->I_2m[0];
  }

  if (ppr2->compute_m[1] == _TRUE_)
    rho_quadrupole_m1    =  rho_g*ppw2->I_2m[1];
  
  if (ppr2->compute_m[2] == _TRUE_)
    rho_quadrupole_m2    =  rho_g*ppw2->I_2m[2];



  // *** Baryons contribution
  rho_b = ppw2->pvecback[pba->index_bg_rho_b];
  
  if (ppr2->compute_m[0] == _TRUE_) {
    rho_monopole           +=  rho_b*b(0,0,0);
    rho_dipole             +=  rho_b*b(1,1,0);
    rho_quadrupole         +=  rho_b*ppw2->b_22m[0];
  }
  
  if (ppr2->compute_m[1] == _TRUE_)
    rho_quadrupole_m1    +=  rho_b*ppw2->b_22m[1];

  if (ppr2->compute_m[2] == _TRUE_)
    rho_quadrupole_m2    +=  rho_b*ppw2->b_22m[2];




  // *** CDM contribution
  if (pba->has_cdm == _TRUE_) {

    rho_cdm = ppw2->pvecback[pba->index_bg_rho_cdm];
    
    if (ppr2->compute_m[0] == _TRUE_) {
      rho_monopole          +=  rho_cdm*cdm(0,0,0);
      rho_dipole            +=  rho_cdm*cdm(1,1,0);
      rho_quadrupole        +=  rho_cdm*ppw2->cdm_22m[0];
    }
    
    if (ppr2->compute_m[1] == _TRUE_)
      rho_quadrupole_m1   +=  rho_cdm*ppw2->cdm_22m[1];

    if (ppr2->compute_m[2] == _TRUE_)
      rho_quadrupole_m2   +=  rho_cdm*ppw2->cdm_22m[2];

  }


  
  // *** Ultra-relativistic neutrino/relics contribution
  if (pba->has_ur == _TRUE_) {

    rho_ur = ppw2->pvecback[pba->index_bg_rho_ur];

    if (ppr2->compute_m[0] == _TRUE_) {
      rho_monopole           +=  rho_ur*N(0,0);
      rho_dipole             +=  rho_ur*N(1,0);
      rho_quadrupole         +=  rho_ur*N(2,0);
    }

    if (ppr2->compute_m[1] == _TRUE_)
      rho_quadrupole_m1    +=  rho_ur*N(2,1);

    if (ppr2->compute_m[2] == _TRUE_)
      rho_quadrupole_m2    +=  rho_ur*N(2,2);

  }


  
  
  // ==============================================================================================
  // =                                    Einstein equations                                      =
  // ==============================================================================================

  /* Newtonian gauge */
  if (ppt->gauge == newtonian) {


    // --------------------------------------------------------------
    // -                      Scalar potentials                     -
    // --------------------------------------------------------------

    if (ppr2->compute_m[0] == _TRUE_) {

      /* Constraint equation for psi. This is just the anisotropic stresses Einstein
      equation.  If you consider that (w+1)*shear = 2/15 quadrupole, this matches the
      equation in the original CLASS */
      ppw2->pvecmetric[ppw2->index_mt2_psi] =
        y[ppw2->pv->index_pt2_phi]
        - 3/5. * (a_sq/k_sq) * rho_quadrupole
        + ppw2->pvec_quadsources[ppw2->index_qs2_psi];


      /* Phi' from the Einstein time-time (Poisson) equation */
      ppw2->pvecmetric[ppw2->index_mt2_phi_prime_poisson] = 
        - Hc*ppw2->pvecmetric[ppw2->index_mt2_psi]
        - k_sq/(3*Hc)*y[ppw2->pv->index_pt2_phi]
        - a_sq * rho_monopole/(2*Hc)
        + ppw2->pvec_quadsources[ppw2->index_qs2_phi_prime_poisson];
   

      /* Phi' from the longitudinal Einstein equation.  If you consider that
      (w+1)*theta = k*dipole/3, you recover the equation in the original CLASS. */
      ppw2->pvecmetric[ppw2->index_mt2_phi_prime_longitudinal] = 
        - Hc*ppw2->pvecmetric[ppw2->index_mt2_psi]
        + 0.5 * (a_sq/k) * rho_dipole
        + ppw2->pvec_quadsources[ppw2->index_qs2_phi_prime_longitudinal];


      /* Choose which equation to use */
      if (ppt2->phi_prime_eq == poisson)  {
        ppw2->pvecmetric[ppw2->index_mt2_phi_prime] =  ppw2->pvecmetric[ppw2->index_mt2_phi_prime_poisson];
      }
      else if (ppt2->phi_prime_eq == longitudinal) {
        ppw2->pvecmetric[ppw2->index_mt2_phi_prime] =  ppw2->pvecmetric[ppw2->index_mt2_phi_prime_longitudinal];
      }

    } // end of scalar potentials


    // -------------------------------------------------------
    // -                  Vector  potentials                 -
    // -------------------------------------------------------
    

    if (ppr2->compute_m[1] == _TRUE_) {

      ppw2->pvecmetric[ppw2->index_mt2_omega_m1_prime] =
        - 2 * Hc * y[ppw2->pv->index_pt2_omega_m1]
        + 2 * sqrt_3/(5.*k) * a_sq * rho_quadrupole_m1
        + ppw2->pvec_quadsources[ppw2->index_qs2_omega_m1_prime];

    } // end of vector potential


    // -------------------------------------------------------
    // -                   Tensor  potential                 -
    // -------------------------------------------------------

    if (ppr2->compute_m[2] == _TRUE_) {

      ppw2->pvecmetric[ppw2->index_mt2_gamma_m2_prime_prime] =
        - 2 * Hc * y[ppw2->pv->index_pt2_gamma_m2_prime]
        - k_sq * y[ppw2->pv->index_pt2_gamma_m2]
        - 2/5. * a_sq * rho_quadrupole_m2
        + ppw2->pvec_quadsources[ppw2->index_qs2_gamma_m2_prime_prime];

    } // end of tensor potential


    /* Some debug */
    // if (ppw2->index_k1==0) {
    //   if (ppw2->index_k2==1) {
    //     fprintf (stderr, "%17.7g %17.7g %17.7g %17.7g %17.7g %17.7g\n", log10(a/pba->a_eq),
    //       ppw2->pvecmetric[ppw2->index_mt2_phi_prime],
    //       ppw2->pvecmetric[ppw2->index_mt2_phi_prime_poisson],
    //       ppw2->pvecmetric[ppw2->index_mt2_phi_prime_longitudinal],
    //       0.5 * (a_sq/k) * rho_g*ppw2->I_2m[0];
    //       0.5 * (a_sq/k) * rho_b*b_110
    //       );
    //   }
    // }


  } // end of if(newtonian)


  /* Synchronous gauge */
  if (ppt->gauge == synchronous) {

    if (ppr2->compute_m[0] == _TRUE_) {

      // // =========================================
      // // = h_prime - TIME-TIME EINSTEIN EQUATION =
      // // =========================================
      // // Eq. 21a of Ma & Berty
      // /* first equation involving total density fluctuation */
      // ppw2->pvecmetric[ppw2->index_mt2_h_prime] =
      //        ( k_sq * y[ppw2->pv->index_pt2_eta] + 1.5 * a_sq * delta_rho)/(0.5*Hc)
      //        + ppw2->pvec_quadsources[ppw2->index_qs2_h_prime];
      // 
      // /* infer streaming approximation for gamma and
      //         correct the total velocity */
      // // Skipped 47 lines of rsa approximation
      //     
      // 
      // // ==============================================
      // // = eta_prime - LONGITUDINAL EINSTEIN EQUATION =
      // // ==============================================
      // // Eq. 21b of Ma & Berty
      // /* second equation involving total velocity */
      // ppw2->pvecmetric[ppw2->index_mt2_eta_prime] = 1.5 * (a_sq/k_sq) * rho_plus_p_theta
      //   + ppw2->pvec_quadsources[ppw2->index_qs2_eta_prime];
      // 
      // 
      // 
      // // ==============================================
      // // = h_prime_prime - TRACE OF EINSTEIN EQUATION =
      // // ==============================================
      // // Eq. 21c of Ma & Berty
      // /* third equation involving total pressure */
      // ppw2->pvecmetric[ppw2->index_mt2_h_prime_prime] = 
      //        -2.*Hc*ppw2->pvecmetric[ppw2->index_mt2_h_prime]
      //        +2.*k_sq*y[ppw2->pv->index_pt2_eta]
      //        -9.*a_sq*delta_p
      //        + ppw2->pvec_quadsources[ppw2->index_qs2_h_prime_prime];
      // 
      // /* intermediate quantity: alpha = (h'+6eta')/2k^2 */
      // double alpha = (ppw2->pvecmetric[ppw2->index_mt2_h_prime] + 6.*ppw2->pvecmetric[ppw2->index_mt2_eta_prime])/2./k_sq;
      // 
      // /* eventually, infer first-order tight-coupling approximation for photon
      //        shear, then correct the total shear */
      // // Skipped 7 lines of tca
      // 
      // 
      // 
      // // ===============================================
      // // = alpha_prime - ANISOTROPIC STRESSES EQUATION =
      // // ===============================================
      // // Eq. 21d of Ma & Berty    
      // /* fourth equation involving total shear */
      // /* alpha' = (h''+6eta'')/2k_sq */      
      // ppw2->pvecmetric[ppw2->index_mt2_alpha_prime] = 
      //        - 4.5 * (a_sq/k_sq) * rho_plus_p_shear + y[ppw2->pv->index_pt2_eta] - 2.*Hc*alpha
      //        + ppw2->pvec_quadsources[ppw2->index_qs2_alpha_prime];
      // 
      // /* getting phi here is an option */
      // /* phi=y[ppw2->pv->index_pt2_eta]-0.5 * (Hc/k_sq) * (h_plus_six_eta_prime); */   
      // /* phi from gauge transformation (from synchronous to newtonian) */

    } // end of scalar potentials

  } // end of if(synchronous)


  return _SUCCESS_;

}  // end of perturb2_einstein



/**
 * Update the shared variables in the workspace with their current value.
 *
 * The ppw2 workspace is passed by several SONG functions involved with the
 * evolution of the second-order differential system; as such, it is the
 * natural place where to put shared variables.
 * 
 * An example of a shared variable is one that depends on the current
 * approximation scheme and needs to be updated at each time step of the
 * differential evolver; the photon quadrupole is one of such variables.
 *
 * This function requires:
 * -# background_at_tau()
 * -# thermodynamics_at_z()
 *
 */
int perturb2_workspace_at_tau (
       struct precision * ppr,
       struct precision2 * ppr2,
       struct background * pba,
       struct thermo * pth,
       struct perturbs * ppt,
       struct perturbs2 * ppt2,
       double tau,
       double * y,
       struct perturb2_workspace * ppw2
       )
{

  // ----------------------------------------------------------------------------------------
  // -                                   Perfect fluid                                      -
  // ----------------------------------------------------------------------------------------

  /* Update the value of the baryon n=2 moments. If the baryons are NOT treated like a 
  perfect fluid, then these moments (corresponding to pressure and shear) are evolved
  quantities and can be accessed directly with the macros b(n,l,m). */

  if (ppt2->has_perfect_baryons == _FALSE_) { 
    ppw2->b_200 = b(2,0,0);
    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppt2->m[index_m];
      ppw2->b_22m[m] = b(2,2,m);
    }
  }
  
  /* Otherwise, if the baryons are treated as a perfect fluid, set their pressure
  and shear to vanish by giving appropriate values to the moments b_200 and b_22m;
  same for the cold dark matter. We store these values in ppw2->b_200,
  ppw2->b_22m[m], ppw2->cdm_200, ppw2->cdm_22m[m]. For details, see eq. 5.18 of
  http://arxiv.org/abs/1405.2280. */
  
  else {
    
    /* Baryon contribution to b200 and b22m, assuming no pressure */
    double vv_b = ppw2->pvec_quadsources[ppw2->index_qs2_vv_b];
    ppw2->b_200 = - 2 * ppw2->k1_dot_k2 * vv_b;
    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppt2->m[index_m];
      ppw2->b_22m[m] = 15 * ppw2->k1_ten_k2[m+2] * vv_b;
    }  

    /* Uncomment to include the effective contribution of baryon pressure. See my May 2014 notes
    on "Baryon eqs including pressure". Note that b_200 in this case still reduces to the simple
    version (-2*k1_dot_k2*vv_b) when cb2=0.
    TODO: include same modification in the baryon quadratic sources for continuity and Euler equations.
    To do so, look in get_boltzmann_equation.nb for the quadratic terms of the monopole and dipole
    equations for the massive hierarchy, and include in SONG the red 2Delta00 terms as
    3 * cb2 * delta_b */
    // double cb2 = pvecthermo[pth->index_th_cb2];    /* Baryon sound speed */
    // double cb2_dot = pvecthermo[pth->index_th_dcb2];    /* Derivative wrt conformal time of baryon sound speed */
    //
    // double dd_b = ppw2->pvec_quadsources[ppw2->index_qs2_dd_b];
    // ppw2->b_200 =  3 * cb2 * b(0,0,0) - 2 * (1-3*cb2) * ppw2->k1_dot_k2 * vv_b + cb2_dot/Hc * dd_b;
    //
    // if ((ppw2->index_k1==(ppt2->k_size-1)) && (ppw2->index_k2==(ppt2->k_size-1)) && (ppw2->index_k3==(ppt2->k_size-1)))
    //   printf ("%17g %17g %17g\n", tau, -2*ppw2->k1_dot_k2*vv_b, ppw2->b_200);
    // }

    /* TODO: include effective shear in b22m, in the same way we introduce the effective pressure
    in b200. I need to define another effective quantity like the sound of speed, viscosity? */
  }

  /* Same for cold dark matter */
  if (pba->has_cdm == _TRUE_) {
    if (ppt2->has_perfect_cdm == _FALSE_) {
      ppw2->cdm_200 = cdm(2,0,0);
      for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
        int m = ppt2->m[index_m];
        ppw2->cdm_22m[m] = cdm(2,2,m);
      }
    }
    else {
      double vv_cdm = ppw2->pvec_quadsources[ppw2->index_qs2_vv_cdm];
      ppw2->cdm_200 = - 2 * ppw2->k1_dot_k2 * vv_cdm;
      for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
        int m = ppt2->m[index_m];
        ppw2->cdm_22m[m] = 15 * ppw2->k1_ten_k2[m+2] * vv_cdm;
      }
    }
  }


  // ----------------------------------------------------------------------------------------
  // -                                  Tight coupling                                      -
  // ----------------------------------------------------------------------------------------

  /* If the tight coupling approximation is turned off, then the photon dipole and
  quadrupoles are evolved normally and can be accessed with the macros I(l,m), E(l,m)
  and B(l,m). */
  
  if (ppw2->approx[ppw2->index_ap2_tca] == (int)tca_off) {

    for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
      int m = ppt2->m[index_m];
      ppw2->I_1m[m] = I(1,m);
    }
    
    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppt2->m[index_m];
      ppw2->I_2m[m] = I(2,m);
      if (ppt2->has_polarization2 == _TRUE_) {
        ppw2->E_2m[m] = E(2,m);
        ppw2->B_2m[m] = B(2,m);
      }
    }

    /* Purely second-order part of the dipole collision term of photons. The
    collision term of baryons is the same times -r=-rho_g/rho_b */
    for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
      int m = ppt2->m[index_m];
      double kappa_dot = ppw2->pvecthermo[pth->index_th_dkappa];
      ppw2->C_1m[m] = kappa_dot * (four_thirds*b(1,1,m) - I(1,m));
     	/* magnetic fileds need the slip, this is a bit dirty here */
     	/* ppw2->U_slip_tca1[m] =  (four_thirds*b(1,1,m) - I(1,m))/4.;*/
     	
    }
    
  }

  /* If the tight coupling approximation is turned on, then we infer the photon 
  quadrupoles and the velocity slip using the TCA relations. */
  
  else {
    
    /* Infer the value of the photon quadrupoles and of the velocity slip, and store
    them in ppw2. */
    class_call (perturb2_tca_variables(
                  ppr,
                  ppr2,
                  pba,
                  pth,
                  ppt,
                  ppt2,
                  tau,
                  y,
                  ppw2),
      ppt2->error_message,
      ppt2->error_message);

    /* Tell SONG to use the TCA values rather than the evolved ones */
    for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
      int m = ppt2->m[index_m];
      ppw2->I_1m[m] = ppw2->I_1m_tca1[m];
    }

    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppt2->m[index_m];
      ppw2->I_2m[m] = ppw2->I_2m_tca1[m];
      if (ppt2->has_polarization2 == _TRUE_) {
        ppw2->E_2m[m] = ppw2->E_2m_tca1[m];
        ppw2->B_2m[m] = (m!=0?ppw2->B_2m_tca1[m]:0);
      }
    }
    
    /* In TCA, the collision term is basically just the velocity slip, which
    is computed in perturb2_tca_variables() */
    for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
      int m = ppt2->m[index_m];
      ppw2->C_1m[m] = ppw2->C_1m_tca1[m];
    }

  } // end of if(tca_on)

  /* Debug - print the polarisation quadrupole */
  // if ((ppw2->index_k1 == 19) && (ppw2->index_k2==16) && (ppw2->index_k3==11))
  //   fprintf (stderr, "%12g %12g\n", tau, ppw2->E_2m[0]);

  return _SUCCESS_;

}




/**
 * Compute and store the fluid variables (density, velocity, pressure and shear)
 * for the considered wavemode and time.
 *
 * Contrary to CLASS, SONG evolves the moments of the distribution function rather
 * than the fluid variables. The former are more suitable for a general treatment,
 * while the latter are defined only up to l=2, because they ultimately stem from
 * a parametrisation of the energy-momentum tensor which is a rank-2 tensor.
 * However, the physical interpretation of the fluid variables is more immediate;
 * for example, during tight coupling it is the shear that vanishes, not the
 * quadrupole.
 *
 * At first order, the moments are linearly related to the fluid variables; for
 * example, the dipole of a given species is just 3i(w+1) times its velocity. At
 * second order, one has to add quadratic terms in the velocity, which encode the
 * Lorentz boost between the local inertial frame to the fluid's rest frame.
 *
 * The relation between moments and fluid variables are derived in sec. 4.2.4 of
 * my PhD thesis (http://arxiv.org/abs/1405.2280). In particular, here we implement
 * the formulas in eq. 4.48:
 *
 *     000 = delta             +   (w+1) v^i v_i
 *     11m = 3 (w+1) i v[m]    +   3 (1+cs^2) delta i v[m]
 *     200 = 3 pressure        +   (w+1) v^i v_i
 *     22m = -15/2 shear[m]    -   15/2 (w+1) vv[m]
 *
 * where vv(m) = X[m]^ij v_i v_j and X[m]^ij is the matrix that represents the n^i*n^j
 * in multipole space. Note that in SONG we adopt the notation u[m]=i*v[m] in order to
 * avoid dealing with imaginary quantities.
 *
 * For example, for the massless photons we have:
 *
 *     I(0,0) = delta_g + 4/3 v_g^i v_g_i
 *     I(1,m) = 4 (u_g[m] + delta_g u_g[m])
 *     pressure_g = delta_g/3
 *     I(2,m) = -15/2 shear_g[m] - 10 vv_g[m]
 *
 * while for the massive baryons we have:
 *
 *     b(0,0,0) = delta_b + v_b^i v_b_i
 *     b(1,1,m) = 3 (u_b[m] + delta_b u_b[m])
 *     b(2,0,0) = 3 pressure_g + v^i v_i
 *     b(2,2,m) = -15/2 (shear_b[m] + vv_g[m]) .
 *
 * This function requires the following functions to be called beforehand:
 * -# perturb_song_sources_at_tau() to fill ppw2->pvec_sources1 and ppw2->pvec_sources2
 * -# 
 * can be called at any point during the evolution of the differential
 * system. It does not require any background or thermodynamics quantity, but it requires
 * the y vector to be filled. If you are interested in the photon shear (ppw2->shear_g),
 * call this function after perturb2_einstein(), where the photon quadrupole (ppw2->I_2m)
 * is set. Otherwise, just ignore the output value in ppw2->shear_g.
 * 
 *
 * @section Velocities in SONG
 *
 * In SONG, we decompose the 4-velocity of each species as U^mu = (U^0, v^i/a). Only three
 * of the four components are independent because of the normalisation constraint
 * U^mu U_mu = -1; we choose to use the three spatial components v^i and express U^0
 * accordingly (see eq. 3.86 of http://arxiv.org/abs/1405.2280).
 *
 * To simplify the equations, we further decompose the spatial velocity v^i in its scalar
 * (m=0) and vector (|m|=1) parts:
 *     v[m] = xi[m]^i v_i,
 * where xi[m]^i are the projection vectors; for more detail on this projection and on
 * the scalar-vector-tensor decomposition, see sec. A.3, 3.3 and 3.6.2 of
 * http://arxiv.org/abs/1405.2280.
 *
 * At first order, we take all velocities to be irrotational which means that v^i is
 * expressed in terms of a single scalar potential v:
 *    v^i = dv/dx_i -> v^i(k) = i k^i v(k)
 * In multipole space:
 *    v[m](k) = i k[m] v(k).
 * In terms of u[m]=i*v[m]:
 *    u[m](k) = -k[m] v(k).
 *
 */

int perturb2_fluid_variables (
       struct precision * ppr,
       struct precision2 * ppr2,
       struct background * pba,
       struct thermo * pth,
       struct perturbs * ppt,
       struct perturbs2 * ppt2,
       double tau,
       double * y,
       struct perturb2_workspace * ppw2
       )
{
  
  /* - Shortcuts */

  double * k1_m = ppw2->k1_m;
  double * k2_m = ppw2->k2_m;
  double k1_dot_k2 = ppw2->k1_dot_k2;
  double * k1_ten_k2 = ppw2->k1_ten_k2;


  /* - Interpolate first order densities and velocities */
  /* THESE TWO CALLS CAN BE REMOVED IF WE KEEP QUADRATIC_SOURCES IN DERIVS */ 
  class_call (perturb_song_sources_at_tau (
               ppr,
               ppt,
               ppt->index_md_scalars,
               ppt2->index_ic_first_order,
               ppw2->index_k1,
               tau,
               ppt->qs_size_short, /* just delta and vpot */
               ppt->inter_normal,
               &(ppw2->last_index_sources),
               ppw2->pvec_sources1),
     ppt->error_message,
     ppt2->error_message);

  class_call (perturb_song_sources_at_tau (
               ppr,
               ppt,
               ppt->index_md_scalars,
               ppt2->index_ic_first_order,
               ppw2->index_k2,
               tau,
               ppt->qs_size_short, /* just delta and vpot */
               ppt->inter_normal,
               &(ppw2->last_index_sources),                 
               ppw2->pvec_sources2),
    ppt->error_message,
    ppt2->error_message);



  // ======================================================================================
  // =                               Photon fluid variables                               =
  // ======================================================================================

  double vpot_g_1 = ppw2->pvec_sources1[ppt->index_qs_v_g];
  double vpot_g_2 = ppw2->pvec_sources2[ppt->index_qs_v_g];
  
  double delta_g_1 = ppw2->delta_g_1 = ppw2->pvec_sources1[ppt->index_qs_delta_g];
  double delta_g_2 = ppw2->delta_g_2 = ppw2->pvec_sources2[ppt->index_qs_delta_g];
  double v_dot_v_g = ppw2->v_dot_v_g = - k1_dot_k2 * vpot_g_1 * vpot_g_2;
      
  if (ppr2->compute_m[0] == _TRUE_) {
    ppw2->delta_g = I(0,0) - 8/3.*v_dot_v_g;
    ppw2->pressure_g = ppw2->delta_g/3;
    /* delta_g_adiab is the second-order expansion of delta_g^1/4 minus the
    quadratic part of delta_b^1/3. For adiabatic initial conditions where
    delta_g^1/4 = delta_b^1/3, it is supposed to be equal to 4/3 * delta_b^(2)
    during tight coupling. */
    ppw2->delta_g_adiab = I(0,0) - delta_g_1*delta_g_2/4.;
  }

  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    int m = ppr2->m[index_m];
    ppw2->u_g_1[m] = -k1_m[m+1]*vpot_g_1;
    ppw2->u_g_2[m] = -k2_m[m+1]*vpot_g_2;
    ppw2->u_g[m] = ppw2->I_1m[m]/4 - delta_g_1*ppw2->u_g_2[m] - delta_g_2*ppw2->u_g_1[m];
  }

  for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
    int m = ppr2->m[index_m];
    ppw2->v_ten_v_g[m] = - k1_ten_k2[m+2] * vpot_g_1 * vpot_g_2;
    ppw2->shear_g[m] = -2/15.*ppw2->I_2m[m] - 8/3.*ppw2->v_ten_v_g[m];
  }


  
  // ======================================================================================
  // =                               Baryon fluid variables                               =
  // ======================================================================================

  double vpot_b_1 = ppw2->pvec_sources1[ppt->index_qs_v_b];
  double vpot_b_2 = ppw2->pvec_sources2[ppt->index_qs_v_b];
  double vpot_b_1_prime = ppw2->pvec_sources1[ppt->index_qs_v_b_prime];
  double vpot_b_2_prime = ppw2->pvec_sources2[ppt->index_qs_v_b_prime];
  
  double delta_b_1 = ppw2->delta_b_1 = ppw2->pvec_sources1[ppt->index_qs_delta_b];
  double delta_b_2 = ppw2->delta_b_2 = ppw2->pvec_sources2[ppt->index_qs_delta_b];
  double delta_b_1_prime = ppw2->delta_b_1_prime = ppw2->pvec_sources1[ppt->index_qs_delta_b_prime];
  double delta_b_2_prime = ppw2->delta_b_2_prime = ppw2->pvec_sources2[ppt->index_qs_delta_b_prime];
  double v_dot_v_b = ppw2->v_dot_v_b = - k1_dot_k2 * vpot_b_1 * vpot_b_2;
  
  if (ppr2->compute_m[0] == _TRUE_) {
    ppw2->delta_b = b(0,0,0) - 2*v_dot_v_b;
    ppw2->pressure_b = (ppw2->b_200 - 2*v_dot_v_b)/3;
  }

  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    int m = ppr2->m[index_m];
    ppw2->u_b_1[m] = -k1_m[m+1]*vpot_b_1;
    ppw2->u_b_2[m] = -k2_m[m+1]*vpot_b_2;
    ppw2->u_b_1_prime[m] = -k1_m[m+1]*vpot_b_1_prime;
    ppw2->u_b_2_prime[m] = -k2_m[m+1]*vpot_b_2_prime;
    ppw2->u_b[m] = b(1,1,m)/3 - delta_b_1*ppw2->u_b_2[m] - delta_b_2*ppw2->u_b_1[m];
  }

  for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
    int m = ppr2->m[index_m];
    ppw2->v_ten_v_b[m] = - k1_ten_k2[m+2] * vpot_b_1 * vpot_b_2;
    ppw2->v_ten_v_b_prime[m] = - k1_ten_k2[m+2] * (vpot_b_1*vpot_b_2_prime + vpot_b_2*vpot_b_1_prime);
    ppw2->shear_b[m] = -2/15.*ppw2->b_22m[m] - 2*ppw2->v_ten_v_b[m];
  }

  /* Note that, for m=1, the pure and quadratic parts of u_b[1] are exactly equal at late
  times and give rise to a cancellation. Therefore, don't fully trust the velocities at
  late times. To see the cancellation, uncomment the following lines. */
  // int m=1;
  // if (index_tau == ppt2->tau_size-2)
  //   printf("m=1: pure=%18f, quad=%18f, diff=%18f\n",
  //     b(1,1,m)/3.,
  //     ppw2->delta_b_1*ppw2->u_b_2[m] + ppw2->delta_b_2*ppw2->u_b_1[m],
  //     b(1,1,m)/3. - ppw2->delta_b_1*ppw2->u_b_2[m] - ppw2->delta_b_2*ppw2->u_b_1[m]);
    


  // ======================================================================================
  // =                                CDM fluid variables                                 =
  // ======================================================================================

  if (pba->has_cdm == _TRUE_) {

    /* In synchronous gauge there is no CDM velocity. Therefore we initialise the first-order
    (vpot_cdm_1 and vpot_cdm_2) and second-order (u_cdm) velocities to zero. All other
    velocities (v_dot_v, v_ten_v, ...) depend on them, so will be automatically set to zero
    without the need to further check the gauge. */

    double vpot_cdm_1 = 0;
    double vpot_cdm_2 = 0;
    for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
      int m = ppr2->m[index_m];
      ppw2->u_cdm[m] = 0;
    }

    /* Now compute the fluid variables normally */

    if (ppt->gauge != synchronous) {
      vpot_cdm_1 = ppw2->pvec_sources1[ppt->index_qs_v_cdm];
      vpot_cdm_2 = ppw2->pvec_sources2[ppt->index_qs_v_cdm];
    }

    double delta_cdm_1 = ppw2->delta_cdm_1 = ppw2->pvec_sources1[ppt->index_qs_delta_cdm];
    double delta_cdm_2 = ppw2->delta_cdm_2 = ppw2->pvec_sources2[ppt->index_qs_delta_cdm];
    double v_dot_v_cdm = ppw2->v_dot_v_cdm = - k1_dot_k2 * vpot_cdm_1 * vpot_cdm_2;
  
    if (ppr2->compute_m[0] == _TRUE_) {
      ppw2->delta_cdm = cdm(0,0,0) - 2*v_dot_v_cdm;
      ppw2->pressure_cdm = (ppw2->cdm_200 - 2*v_dot_v_cdm)/3;
    }

    for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
      int m = ppr2->m[index_m];
      ppw2->u_cdm_1[m] = -k1_m[m+1]*vpot_cdm_1;
      ppw2->u_cdm_2[m] = -k2_m[m+1]*vpot_cdm_2;
      if (ppt->gauge != synchronous)
        ppw2->u_cdm[m] = cdm(1,1,m)/3 - delta_cdm_1*ppw2->u_cdm_2[m] - delta_cdm_2*ppw2->u_cdm_1[m];
    }

    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppr2->m[index_m];
      ppw2->v_ten_v_cdm[m] = - k1_ten_k2[m+2] * vpot_cdm_1 * vpot_cdm_2;
      ppw2->shear_cdm[m] = -2/15.*ppw2->cdm_22m[m] - 2*ppw2->v_ten_v_cdm[m];
    }
  }
  
  
  
  // ======================================================================================
  // =                              Neutrino fluid variables                              =
  // ======================================================================================

  if (pba->has_ur == _TRUE_) {
      
    double vpot_ur_1 = ppw2->pvec_sources1[ppt->index_qs_v_ur];
    double vpot_ur_2 = ppw2->pvec_sources2[ppt->index_qs_v_ur];
  
    double delta_ur_1 = ppw2->delta_ur_1 = ppw2->pvec_sources1[ppt->index_qs_delta_ur];
    double delta_ur_2 = ppw2->delta_ur_2 = ppw2->pvec_sources2[ppt->index_qs_delta_ur];
    double v_dot_v_ur = ppw2->v_dot_v_ur = - k1_dot_k2 * vpot_ur_1 * vpot_ur_2;
  
    if (ppr2->compute_m[0] == _TRUE_) {
      ppw2->delta_ur = N(0,0) - 8/3.*v_dot_v_ur;
      ppw2->pressure_ur = ppw2->delta_ur/3;
    }

    for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
      int m = ppr2->m[index_m];
      ppw2->u_ur_1[m] = -k1_m[m+1]*vpot_ur_1;
      ppw2->u_ur_2[m] = -k2_m[m+1]*vpot_ur_2;
      ppw2->u_ur[m] = N(1,m)/4 - delta_ur_1*ppw2->u_ur_2[m] - delta_ur_2*ppw2->u_ur_1[m];
    }

    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppr2->m[index_m];
      ppw2->v_ten_v_ur[m] = - k1_ten_k2[m+2] * vpot_ur_1 * vpot_ur_2;
      ppw2->shear_ur[m] = -2/15.*N(2,m) - 8/3.*ppw2->v_ten_v_ur[m];
    }
  }

  return _SUCCESS_;
  
}




/**
 * Compute the velocity slip and the photon quadrupoles and shear in the tight coupling
 * limit.
 *
 * The function compute the following perturbations:
 *
 * - ppw2->U_slip_tca1[m]:     difference between the baryon and photon velocity u_b[m]-u_g[m].
 * - ppw2->I_1m_tca1[m]:   photon intensity dipole up to order O(tau_c)^2.
 * - ppw2->I_2m_tca0[m]:   photon intensity quadrupole up to order O(tau_c).
 * - ppw2->I_2m_tca1[m]:   photon intensity quadrupole up to order O(tau_c)^2.
 * - ppw2->shear_g_tca1[m]:     photon shear up to order O(tau_c)^2.
 * - ppw2->C_1m_tca1[m]:     photon dipole collision term up to order O(tau_c)^2.
 * - ppw2->Pi_tca0[m]:          factor Pi=(I(2,m)-sqrt_6*E(2,m))/10 up to order O(tau_c).
 * - ppw2->Pi_tca1[m]:          factor Pi=(I(2,m)-sqrt_6*E(2,m))/10 up to order O(tau_c)^2.
 * - ppw2->E_2m_tca1[m]:   photon E-mode polarisation quadrupole up to order O(tau_c)^2.
 * - ppw2->B_2m_tca1[m]:   photon B-mode polarisation quadrupole up to order O(tau_c)^2.
 *
 * This function requires the previous execution of the following functions:
 * -# background_at_tau()
 * -# thermodynamics_at_z()
 * -# perturb2_quadratic_sources_at_tau()
 */

int perturb2_tca_variables (
       struct precision * ppr,
       struct precision2 * ppr2,
       struct background * pba,
       struct thermo * pth,
       struct perturbs * ppt,
       struct perturbs2 * ppt2,
       double tau,
       double * y,
       struct perturb2_workspace * ppw2
       )
{

  /* - Shortcuts */
  
  double k = ppw2->k;
  double Hc = ppw2->pvecback[pba->index_bg_a]*ppw2->pvecback[pba->index_bg_H];
  double r = ppw2->pvecback[pba->index_bg_rho_g]/ppw2->pvecback[pba->index_bg_rho_b];
  double kappa_dot = ppw2->pvecthermo[pth->index_th_dkappa]; /* interaction rate */
  double tau_c = 1/kappa_dot; /* life time */


  /* - Compute fluid variables */

  /* Compute density, velocity and shear of photons and baryons, and create 
  local shortcuts. */

  class_call (perturb2_fluid_variables(
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2,
                tau,
                y,
                ppw2),
    ppt2->error_message,
    ppt2->error_message);

  double * u_b = &(ppw2->u_b[0]);
  double * u_b_1 = &(ppw2->u_b_1[0]);
  double * u_b_2 = &(ppw2->u_b_2[0]);
  double * u_b_1_prime = &(ppw2->u_b_1_prime[0]);
  double * u_b_2_prime = &(ppw2->u_b_2_prime[0]);
  double v_dot_v_b = ppw2->v_dot_v_b;
  double * v_ten_v_b = &(ppw2->v_ten_v_b[0]);
  double * v_ten_v_b_prime = &(ppw2->v_ten_v_b_prime[0]);
  double delta_b_1 = ppw2->delta_b_1;
  double delta_b_2 = ppw2->delta_b_2;
  double delta_b_1_prime = ppw2->delta_b_1_prime;
  double delta_b_2_prime = ppw2->delta_b_2_prime;

  double * u_g_1 = &(ppw2->u_g_1[0]);
  double * u_g_2 = &(ppw2->u_g_2[0]);
  double v_dot_v_g = ppw2->v_dot_v_g;
  double * v_ten_v_g = &(ppw2->v_ten_v_g[0]);
  double delta_g_1 = ppw2->delta_g_1;
  double delta_g_2 = ppw2->delta_g_2;
  

  /* - Extract the quadratic collision term */

  if (ppt2->has_quadratic_sources == _TRUE_) {
    class_call (perturb2_quadratic_sources(
                  ppr,
                  ppr2,
                  pba,
                  pth,
                  ppt,
                  ppt2,
                  -1,
                  tau,
                  compute_only_collision,
                  ppw2->pvec_quadsources,
                  ppw2->pvec_quadcollision,
                  ppw2
                  ),
      ppt2->error_message,
      ppt2->error_message);
  }

  /* Uncomment to obtain the collision term by interpolating the precomputed
  table. Interpolating the quadratic sources is quicker, but in the case of
  the TCA quantities it leads to a slower convergence in the differential system;
  in other words, it takes more time steps to evolve the system. The loss in
  convergence speed roughly balances the loss from computing the sources; we choose
  the latter option because it is more precise. */

  // if (ppt2->has_quadratic_sources == _TRUE_) {
  //   class_call(perturb2_quadratic_sources_at_tau(
  //                ppr,
  //                ppr2,
  //                ppt,
  //                ppt2,
  //                tau,
  //                interpolate_collision,
  //                ppw2
  //                ),
  //     ppt2->error_message,
  //     ppt2->error_message);
  // }


  /* - Compute quadratic parts */

  /* Create shortcuts for the quadratic parts of the Boltzmann equation for
  photon intensity, photon polarisation and baryons. */

  double quadC_I_1M=0, quadL_I_1M=0;
  double quadC_I_2M=0, quadL_I_2M=0;
  double quadC_E_2M=0, quadL_E_2M=0;
  double quadC_B_2M=0, quadL_B_2M=0;
  double quadL_b_11M=0;

  for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {

    int m = ppr2->m[index_m];

    /* Quadratic part of the collision term for the multipoles */
    quadC_I_1M = dI_qc2(1,m)/kappa_dot;
    quadC_I_2M = dI_qc2(2,m)/kappa_dot;
    if (ppt2->has_polarization2 == _TRUE_) {
      quadC_E_2M = dE_qc2(2,m)/kappa_dot;
      quadC_B_2M = (m!=0?dB_qc2(2,m):0)/kappa_dot;
    }

    /* Quadratic part of the Liouville term for the multipoles.
    We reverse the sign because in SONG the Liouville term is on the right hand
    side, while in our reference for the TCA equations it is on the left hand
    side. */
    quadL_b_11M = - (db_qs2(1,1,m)-db_qc2(1,1,m));
    quadL_I_1M = - (dI_qs2(1,m)-dI_qc2(1,m));
    quadL_I_2M = - (dI_qs2(2,m)-dI_qc2(2,m));
    if (ppt2->has_polarization2 == _TRUE_) {
      quadL_E_2M = - (dE_qs2(2,m)-dE_qc2(2,m)); /* O(tau_c), see eq. 4.153 */
      quadL_B_2M = - (dB_qs2(2,m)-dB_qc2(2,m)); /* O(tau_c), see eq. 4.156 */
    }
  }
 

  /* - Velocity slip */
  
  /* Compute the velocity slip V[m] = v_b[m] - v_g[m] using the formalism in
  Pitrou 2011 (http://arxiv.org/abs/1012.0546). I take the formula from
  eq. 16 of my TCA notes. The expression matches the one in eq. 13 of Pitrou
  2011, with the (expected) addition of some quadratic terms and a (suspicious)
  switch of sign for delta_g. 
  
  Note that in order to deal with real quantities, we compute U[m]=i*V[m] rather
  than V[m]. */
  
  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {

    int m = ppr2->m[index_m];

    /* Quadratic part of the collision term of the photon velocity;
    see eq. 3b of my TCA notes. */
    double quadC_u_g_m = quadC_I_1M/4
                       + (u_b_1[m]*delta_b_2 + u_b_2[m]*delta_b_1)
                       - (u_g_1[m]*delta_g_2 + u_g_2[m]*delta_g_1);

    /* The velocity slip V=v_b-v_g depends on the difference between the quadratic
    Liouville terms for the baryon (quadL_v_b_m) and photon (quadL_v_g_m) velocities.
    However, SONG computes the Liouville term for the dipoles (b_11M and I_1M) rather
    than for the velocities. Therefore, first we have to perform the dipole->velocity
    transformations in eq. 4.46 of http://arxiv.org/abs/1405.2280:
      b(1,1,m) = 3 u_b[m] + 3 * u_b[m] * delta_b[m]
      I(1,m) = 4 u_g[m] + 4 * u_g[m] * delta_g[m],
    where u[m] is the fluid velocity times the imaginary factor. After applying
    this transformation, a bunch of extra quadratic terms arises, together with
    a 1/3 factor for baryons and 1/4 for photons; see eq. 13b of my TCA notes for
    the full formula, which we write down here. Note that the we got rid of the
    velocity-squared terms. */
    double delta_u_prime_b_m = delta_b_1_prime*u_b_2[m] + delta_b_2_prime*u_b_1[m]
                             + delta_b_1*u_b_2_prime[m] + delta_b_2*u_b_1_prime[m];
    double delta_u_prime_g_m = four_thirds * delta_u_prime_b_m; /* enforce TCA0 */

    double quadL_diff =
      quadL_b_11M/3 - quadL_I_1M/4
      - delta_u_prime_g_m/4 /* from dipole->vel. transf. of dipole derivatives */
      + Hc * (u_b_1[m]*delta_b_2 + u_b_2[m]*delta_b_1); /* from dipole->vel. transf. of Hc*b(11m) */

    /* Expression for the velocity slip V = v_b-v_g in TCA1. The full derivation of 
    the formula can be found in my TCA notes (eq. 16). */
    double R = 3/(4*r); /* Pitrou's definition of R */
    double omega_m1 = (m==1?y[ppw2->pv->index_pt2_omega_m1]:0);

    ppw2->U_slip_tca1[m] = - quadC_u_g_m
                           - R/(1+R)*tau_c * (
                              Hc*(u_b[m] + (m==1?omega_m1:0))
                              + k/4*(m==0?ppw2->delta_g:0)
                              + quadL_diff
                           );

    /* In SONG we write the purely second-order part of the collision term
    as kappa_dot*(4/3*b11m-I_1m). This is just the velocity slip times
    4*kappa_dot, without the quadratic terms in quadC_u_g_m. We rewrite the
    full formula here in order to reduce numerical cancellations. */
    ppw2->C_1m_tca1[m] = - dI_qc2(1,m)
                         - 4*R/(1+R)* (
                            Hc*(u_b[m] + (m==1?omega_m1:0))
                            + k/4*(m==0?ppw2->delta_g:0)
                            + quadL_diff
                         );

    /* During the TCA regime, we do not evolve the photon intensity dipole but
    we obtain it from the velocity slip as v_g[m] = v_b[m] - V[m]. */
    ppw2->u_g_tca1[m] = u_b[m] - ppw2->U_slip_tca1[m];
    ppw2->I_1m_tca1[m] = 4 * (ppw2->u_g_tca1[m] + delta_g_1*u_g_2[m] + delta_g_2*u_g_1[m]);

    /* When we called the perturb2_fluid_variables() function above we did not
    know yet what was the value of the dipole. Now that we do, we update
    the ppw2->u_g[m] field. */
    if (ppw2->approx[ppw2->index_ap2_tca] == (int)tca_off)
      ppw2->u_g[m] = I(1,m)/4 - delta_g_1*u_g_2[m] - delta_g_2*u_g_1[m];
    else
      ppw2->u_g[m] = ppw2->u_g_tca1[m];


    /* The collision term could also be written as: */
    // ppw2->C_1m_tca1[m] = 4 * kappa_dot * (
    //                        + ppw2->U_slip_tca1[m]
    //                        - ppw2->u_g_1[m]*ppw2->delta_g_2 - ppw2->u_g_2[m]*ppw2->delta_g_1
    //                        + ppw2->u_b_1[m]*ppw2->delta_b_2 + ppw2->u_b_2[m]*ppw2->delta_b_1);


    /* Debug - Compute the Liouville difference quadL_diff explicitely as
    quadL_b_11M/3-quadL_I_1M/4 and compare with quadL_diff. */
    // /* Derive the quadratic part of the Liouville term of the baryon velocity */
    // double quadL_u_b_m =
    //   quadL_b_11M/3
    //   + delta_u_prime_b_m
    //   + Hc * (u_b_1[m]*delta_b_2 + u_b_2[m]*delta_b_1)
    //   - 2*k/3 * (15/2.*c_plus(1,m,m)*v_ten_v_b[m] + c_minus(1,m,m)*v_dot_v_b); /* 2 factor from p. exp. */
    //
    // /* Derive the quadratic part of the Liouville term of the photon velocity. */
    // double quadL_u_g_m =
    //   quadL_I_1M/4
    //   + delta_u_prime_g_m
    //   - 2*k * (5/2.*c_plus(1,m,m)*v_ten_v_g[m] + 1/3.*c_minus(1,m,m)*v_dot_v_g); /* 2 factor from p. exp. */
    //
    // /* Compute the difference between the two quadratic Liouville terms, and print the
    // ratio with quadL_diff to output */
    // double quadL_diff_explicit = quadL_u_b_m - quadL_u_g_m;
    //
    // fprintf (stderr, "%17.7g %17.7g %17.7g %17.7g\n",
    //   tau, quadL_diff_explicit/quadL_diff, quadL_diff, quadL_diff_explicit);
    
  }
  

  /* - Photon quadrupole in tight coupling */
  
  for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {

    int m = ppr2->m[index_m];

    /* The photon quadrupole is quadratic in the photon velocity:
      I(2,m) = -10*vv[m] + O(tau_c).
    This follows from the fact that the quadratic part of the quadrupole collision
    term, dI_qc2(2,m)/kappa_dot, is equal to -9*vv[m]+O(tau_c). For a derivation,
    see Eq. 5.57 of http://arxiv.org/abs/1405.2280, which is equivalent to eq.
    C.6 of P2010. An important consequence is that the shear shear_g is is of
    order tau_c; it follows from the relation between the quadrupole and
    the shear:
      I(2,m) = -15/2*shear_g[m] - 10 vv[m]. */
    ppw2->I_2m_tca0[m] = -20*v_ten_v_g[m];

    /* We now proceed to compute the photon shear in tight coupling up to
    terms O(tau_c), following eq. 37 of my TCA notes. Here we write the
    purely second order part of the shear in TCA1. Using CLASS variables,
      shear_g = -3/4 * shear_g[0]
      theta_g = - k * u_g[0] = - k * I(1,m)/4
      c_minus(2,0,0) = 2/3
    the formula reduces to the one implemented in CLASS, that is,
      shear_g = 8/45 * tau_c * shear_g */
    double gamma_m2_prime = (m==2?y[ppw2->pv->index_pt2_gamma_m2_prime]:0);
    ppw2->shear_g_tca1[m] = 8/45. * tau_c * (-k*ppw2->I_1m_tca1[m]*c_minus(2,m,m) - 4*(m==2?gamma_m2_prime:0));
    
    /* The quadratic part is a combination of Liouville terms, collision terms and
    velocity squared terms. Here we add the terms that are not explicitly multiplied
    by a tau_c factor, but are nonetheless of order tau_c. This is the case because
    quadC_I_2M = -9 vv[m] + O(tau_c) and quadC_E_2M = -vv[m] + O(tau_c). */
    ppw2->shear_g_tca1[m] += 8/45. * (-quadC_I_2M + sqrt_6/4*quadC_E_2M - 15*v_ten_v_g[m]);

    /* Here we add the quadratic terms that are multiplied by tau_c. Note that in the
    derivative term we exchange the photon velocity with the baryon velocity, using
    the fact that the whole expression is multiplied by tau_c. Note also that
    the term in quadL_E_2M could be be omitted because quadL_E_2M is O(tau_c)
    (see eq. 4.153 of http://arxiv.org/abs/1405.2280). */
    ppw2->shear_g_tca1[m] += 8/45. * tau_c * (quadL_I_2M - sqrt_6/4*quadL_E_2M - 20*v_ten_v_b_prime[m]);

    /* Cheat and use Pi (eq. 25 of TCA notes) */
    // double Pi = 0.1 * (I(2,m) - sqrt_6*E(2,m));
    // ppw2->shear_g_tca1[m] = -2/15. *
    //   (
    //     Pi + quadC_I_2M + 20 * ppw2->v_ten_v_g[m]
    //     -tau_c * (- 20*v_ten_v_b_prime[m] - k*I(1,m)*c_minus(2,m,m) - 4*(m==2?gamma_m2_prime:0) + quadL_I_2M)
    //   );

    /* Cheat and use E_2M (eq. 25b of TCA notes) */
    // ppw2->shear_g_tca1[m] = -4/27. *
    //   (
    //     quadC_I_2M + 18 * v_ten_v_g[m] - sqrt_6/10*E(2,m)
    //     -tau_c * (-20*v_ten_v_b_prime[m] - k*I(1,m)*c_minus(2,m,m) - 4*(m==2?gamma_m2_prime:0) + quadL_I_2M)
    //   );
    

    /* In absence of polarisation, we take the expression of shear_g as a function of
    Pi, expand Pi as I_2M/10=-3/4*shear_m-vv_m and then solve the equation for shear_m
    (eq. 25b of TCA notes, with E_2M set to zero). */
    if (ppt2->has_polarization2 == _FALSE_) {

      ppw2->shear_g_tca1[m] = -4/27. * (
          quadC_I_2M + 18*v_ten_v_g[m]
          -tau_c * (-20*v_ten_v_b_prime[m] - k*ppw2->I_1m[m]*c_minus(2,m,m)
                    - 4*(m==2?gamma_m2_prime:0) + quadL_I_2M));
    }

    /* The quadrupole is obtained by adding a velocity squared term
    (eq. 4.48 of http://arxiv.org/abs/1405.2280) */
    ppw2->I_2m_tca1[m] = -15/2.*ppw2->shear_g_tca1[m] - 20*v_ten_v_g[m];

    /* When we called the perturb2_fluid_variables() function above we did not
    know yet what was the value of the quadrupole. Now that we do, we update
    the ppw2->shear_g[m] field. */
    if (ppw2->approx[ppw2->index_ap2_tca] == (int)tca_off)
      ppw2->shear_g[m] = -2/15.*I(2,m) - 8/3.*v_ten_v_g[m];
    else
      ppw2->shear_g[m] = ppw2->shear_g_tca1[m];
    
  }
  
  
  /* - Polarisation quadrupole in tight coupling */

  /* Compute the polarisation quadrupole for the E and B-modes during tight
  coupling. In doing so, we also compute the Pi[m] factor which appears in the
  collision term for the intensity and E-polarisation quadrupoles; see eq.
  4.145 of http://arxiv.org/abs/1405.2280. The Pi factor is defined
  as Pi[m] = (I(2,m) - sqrt_6*E(2,m))/10. */
  
  if (ppt2->has_polarization2 == _TRUE_) {
  
    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {

      int m = ppr2->m[index_m];

      /* The TCA0 expression for Pi is simply given by -vv[m], because its parts are
      I(2,m)=-10*vv[m] and dE_qc2(2,m)=-sqrt(6)*vv[m]. */
      ppw2->Pi_tca0[m] = -2*v_ten_v_b[m];

      /* Tight coupling formula (first order in tau_c) for the Pi factor (eq.
      33 of my TCA notes). The last term (quadL) is negligible because it is
      of order O(tau_c). The first two in TCA0 are just proportional to the
      velocity squared; the full expression in TCA0 is simply given by -vv[m]. */
      ppw2->Pi_tca1[m] = (ppw2->I_2m_tca1[m] - sqrt_6*(quadC_E_2M - tau_c*quadL_E_2M))/4;

      /* Use the Boltzmann equation for the quadrupole in TCA1 to obtain the 
      polarisation quadrupole in terms of Pi (eq. 31 of my TCA notes). Note
      that quadL_E_2M, the quadratic part of the Liouville term for polarisation,
      is O(tau_c) and could be omitted without altering the result (see eq. 4.153
      of http://arxiv.org/abs/1405.2280). */
      ppw2->E_2m_tca1[m] = -sqrt(6)*ppw2->Pi_tca1[m]
                          + quadC_E_2M
                          - tau_c*quadL_E_2M;
          
      /* Invert the relation defining Pi to obtain the polarisation quadrupole
      from the intensity quadrupole. Note that this method leads to a cancellation
      because E_2M=O(tau_c) while I_2M=O(1).  */
      // ppw2->E_2m_tca1[m] = (ppw2->I_2m_tca1[m] - 10*ppw2->Pi_tca1[m])/sqrt_6;
      
      /* Another way to express the polarisation quadrupole is in terms of the
      intensity quadrupole (eq. 32 of my TCA notes). */
      // ppw2->E_2m_tca1[m] = -sqrt(6)/4*ppw2->I_2m_tca1[m]
      //                 + 5/2.*quadC_E_2M
      //                 - 5/2.*tau_c*quadL_E_2M;

      /* The collisionless equation for the B-mode quadrupole (eq. 4.147) contains
      only terms that are O(tau_c). For example, the quadratic part of the Liouville term
      (eq. 4.156) only contains the E-mode quadrupole, which is O(tau_c). Therefore, the
      value of B_2M up to O(tau_c)^2 is obtained by equating the collision term to zero,
      as you would do for a TCA0 approximation. */
      ppw2->B_2m_tca1[m] = (m!=0 ? quadC_B_2M : 0);

    }
  }

  return _SUCCESS_;

}


/** 
 * Compute psi_prime, the derivative of the gravitational potential with respect to
 * conformal time in Newtonian gauge, needed for the ISW effect.
 * 
 * We compute psi_prime here rather than in perturb2_einstein() for two reasons. First, 
 * psi_prime is not needed to solve the differential system, and it would be a waste of
 * time to compute it at every step of the integrator. Secondly, to obtain psi_prime
 * we first need to access the velocity and shear derivatives and perturb2_einstein()
 * does not have access to the vector dy that contains them.
 *
 * This function needs to be called after:
 * -# background_at_tau()
 * -# thermodynamics_at_z()
 * -# perturb_song_sources_at_tau()
 * -# perturb2_tca_variables()
 * -# perturb2_einstein()
 *
 * TODO: when you will implement TCA at second order, copy the relevant lines from the first-order
 * equivalent of this function (perturb2_compute_psi_prime)
 */

int perturb2_compute_psi_prime(
       struct precision * ppr,
       struct precision2 * ppr2,
       struct background * pba,
       struct thermo * pth,
       struct perturbs * ppt,
       struct perturbs2 * ppt2,
       double tau,
       double * y,
       double * dy,
       double * psi_prime,
       struct perturb2_workspace * ppw2)
{

  class_test (ppr2->compute_m[0] == _FALSE_,
    ppt2->error_message,
    "stopping to prevent segfault - there is no psi without scalar modes.");

  /* Shortcuts */
  double k = ppw2->k;
  double k2 = k*k;
  double a2 = ppw2->pvecback[pba->index_bg_a]*ppw2->pvecback[pba->index_bg_a];
  double Hc = ppw2->pvecback[pba->index_bg_a]*ppw2->pvecback[pba->index_bg_H];
  double kappa_dot = ppw2->pvecthermo[pth->index_th_dkappa];



  // =============================================================================
  // =                              Compute shear                                =
  // =============================================================================

  /* To compute psi_prime we need rho*d(quadrupole)/dtau and
  rho*(1+3w)*quadrupole. */
  double rho_plus_3p_quadrupole=0, rho_quadrupole_prime=0;

  /* - Photons */
  
  double rho_g = ppw2->pvecback[pba->index_bg_rho_g];

  rho_plus_3p_quadrupole = 2 * rho_g * ppw2->I_2m[0];

  if (ppw2->approx[ppw2->index_ap2_tca] == (int)tca_off) 
    rho_quadrupole_prime = rho_g * dI(2,0);
  else {
    /* For the time derivative of the quadrupole in TCA1, we use the usual Boltzmann
    equation with I(3,0)=0. We also use c_minus(2,0,0)=2/3 */
    double dI_20 = 2/3.*k*ppw2->I_1m_tca1[0] - kappa_dot*(ppw2->I_2m_tca1[0] - ppw2->Pi_tca1[0]);
    rho_quadrupole_prime = rho_g * dI_20;
  }


  /* - Baryons */
  
  double rho_b = ppw2->pvecback[pba->index_bg_rho_g];
  double vpot_b_1 = ppw2->pvec_sources1[ppt->index_qs_v_b];
  double vpot_b_2 = ppw2->pvec_sources2[ppt->index_qs_v_b];
  double vpot_b_1_prime = ppw2->pvec_sources1[ppt->index_qs_v_b_prime];
  double vpot_b_2_prime = ppw2->pvec_sources2[ppt->index_qs_v_b_prime];

  rho_plus_3p_quadrupole += rho_b*ppw2->b_22m[0];
  
  if (ppt2->has_perfect_baryons == _FALSE_) {
    rho_quadrupole_prime += rho_b*db(2,2,0);
  }
  else {
    double db_220 = 15 * ppw2->k1_ten_k2[0+2]
      * (vpot_b_1_prime*vpot_b_2 + vpot_b_1*vpot_b_2_prime);
    rho_quadrupole_prime += rho_b*db_220;
  }
  
  
  /* - Cold dark matter */
  
  if (pba->has_cdm == _TRUE_) {

    double rho_cdm = ppw2->pvecback[pba->index_bg_rho_cdm];
    double vpot_cdm_1 = ppw2->pvec_sources1[ppt->index_qs_v_cdm];
    double vpot_cdm_2 = ppw2->pvec_sources2[ppt->index_qs_v_cdm];
    double vpot_cdm_1_prime = ppw2->pvec_sources1[ppt->index_qs_v_cdm_prime];
    double vpot_cdm_2_prime = ppw2->pvec_sources2[ppt->index_qs_v_cdm_prime];

    rho_plus_3p_quadrupole +=  rho_cdm*ppw2->cdm_22m[0];

    if (ppt2->has_perfect_cdm == _FALSE_) {
      rho_quadrupole_prime += rho_cdm*dcdm(2,2,0);
    }
    else {
      double dcdm_220 = 15 * ppw2->k1_ten_k2[0+2]
        * (vpot_cdm_1_prime*vpot_cdm_2 + vpot_cdm_1*vpot_cdm_2_prime);
      rho_quadrupole_prime += rho_cdm*dcdm_220;
    }

  }
  
  /* Neutrinos */
  
  if (pba->has_ur == _TRUE_) {

    double rho_ur = ppw2->pvecback[pba->index_bg_rho_ur];
    rho_plus_3p_quadrupole += 2*rho_ur*N(2,0);
    rho_quadrupole_prime += rho_ur*dN(2,0);
  }
  
  // =============================================================================
  // =                              Equation for psi'                            =
  // =============================================================================
  
  /* The derivative of the gravitational potential psi, needed to compute the ISW effect */
  *psi_prime = ppw2->pvecmetric[ppw2->index_mt2_phi_prime] - 3/5. * (a2/k2)
    * (rho_quadrupole_prime - Hc*rho_plus_3p_quadrupole)
    + ppw2->pvec_quadsources[ppw2->index_qs2_psi_prime];
  
  /* Debug - print psi_prime together with psi */
  // if (ppw2->index_k == 200)
  //   fprintf (stderr, "%15g %15g %15g\n", tau, ppw2->pvecmetric[ppw2->index_mt2_psi], *psi_prime);

  return _SUCCESS_;

}




/**
  * Compute the quadratic sources for the set of wavemodes (k1,k2,k) given in
  * the ppw2 workspace and for all times contained in ppt->tau_sampling_quadsources.
  *
  * This function loops over time on the perturb2_quadratic_sources() function,
  * which is used to filled the tables.
  *
  * The following arrays will be filled:
  * - ppw2->quadsources_table[index_qs2][index_tau] 
  * - ppw2->quadcollision_table[index_qs2][index_tau] 
  * - ppw2->dd_quadsources_table[index_qs2][index_tau] 
  * - ppw2->dd_quadcollision_table[index_qs2][index_tau] 
  *
  */
int perturb2_quadratic_sources_for_k1k2k (
      struct precision * ppr,
      struct precision2 * ppr2,
      struct background * pba,
      struct thermo * pth,            
      struct perturbs * ppt,
      struct perturbs2 * ppt2,
      struct perturb2_workspace * ppw2
      )
{

  for (int index_tau=0; index_tau<ppt->tau_size_quadsources; ++index_tau) {
    
    double tau = ppt->tau_sampling_quadsources[index_tau];

    /* Interpolate background-related quantities (pvecback) */
    class_call (background_at_tau(
                 pba,
                 tau, 
                 pba->normal_info, 
                 pba->inter_closeby,
                 &(ppw2->last_index_back), 
                 ppw2->pvecback),
      pba->error_message,
      ppt2->error_message);

    /* Interpolate thermodynamics-related quantities (pvecthermo) */
    class_call (thermodynamics_at_z(
                 pba,
                 pth,
                 1./ppw2->pvecback[pba->index_bg_a]-1.,  /* redshift z=1./a-1 */
                 pth->inter_closeby,
                 &(ppw2->last_index_thermo),
                 ppw2->pvecback,
                 ppw2->pvecthermo),
      pth->error_message,
      ppt2->error_message);

    /* Compute the value of the quadratic sources and store them first in
    ppw2->pvec_quadsources, and from there to ppw2->quadsources_table, which
    will be later interpolated. */
    class_call (perturb2_quadratic_sources(
                  ppr,
                  ppr2,
                  pba,
                  pth,            
                  ppt,
                  ppt2,
                  index_tau,
                  1e99, /* tau not used, no interp */
                  compute_total_and_collision,
                  ppw2->pvec_quadsources,
                  ppw2->pvec_quadcollision,
                  ppw2
                  ),
      ppt2->error_message,
      ppt2->error_message);

    for (int index_qs2=0; index_qs2 < ppw2->qs2_size; ++index_qs2)
      ppw2->quadsources_table[index_qs2][index_tau] = ppw2->pvec_quadsources[index_qs2];

    for (int index_qs2=0; index_qs2 < ppw2->qs2_size; ++index_qs2)
      ppw2->quadcollision_table[index_qs2][index_tau] = ppw2->pvec_quadcollision[index_qs2];

  } // end of for (index_tau)

  /* Compute second-order derivatives of the quadratic sources in view of spline interpolation */
  if (ppr->quadsources_time_interpolation == cubic_interpolation) {

    class_call (spline_sources_derivs_two_levels (
                  ppt->tau_sampling_quadsources, /* vector of size tau_size_quadsources */
                  ppt->tau_size_quadsources,
                  ppw2->quadsources_table,
                  ppw2->qs2_size,
                  ppw2->dd_quadsources_table,
                  _SPLINE_EST_DERIV_,
                  // _SPLINE_NATURAL_,
                  ppt2->error_message
                  ),
      ppt2->error_message,
      ppt2->error_message);

    class_call (spline_sources_derivs_two_levels (
                  ppt->tau_sampling_quadsources, /* vector of size tau_size_quadsources */
                  ppt->tau_size_quadsources,
                  ppw2->quadcollision_table,
                  ppw2->qs2_size,
                  ppw2->dd_quadcollision_table,
                  _SPLINE_EST_DERIV_,
                  // _SPLINE_NATURAL_,
                  ppt2->error_message
                  ),
      ppt2->error_message,
      ppt2->error_message);

  } // end of if(cubic_interpolation)
    
  return _SUCCESS_;

}


/**
 * Compute and store in pvec_quadsources the quadratic sources for the second-order
 * differential system.
 *
 * Most equations are taken from http://arxiv.org/abs/1405.2280, sec. 4.6.2. Those
 * for the photon intensity and polarisation were first derived by Pitrou, Uzan
 * and Bernardeau 2010, and match those from Beneke, Fidler & Klingmuller 2010 and
 * 2011.
 *
 * By default, compute the quadratic sources at the time value contained in
 * ppt->tau_sampling_quadsources[index_tau]. This is the most economic way
 * to call this function because the first-order perturbations do not need
 * to be interpolated.
 *
 * If index_tau is a negative value, then the quadratic sources are interpolated
 * at the time tau.
 *
 * This function requires:
 * -# background_at_tau()
 * -# thermodynamics_at_z()
 *
 * @section Symmetrisation of quadratic terms
 *
 * In SONG we symmetrise all quadratic sources, but we never write down the 1/2 factor
 * that stems from the symmetrization. The reason is that it is cancelled by a 2 factor
 * arising from our choice of convention for the perturbative expansion:
 *    X ~ X^(0) + X^(1) * 1/2 X^(2),
 * as opposed to the other popular convention 
 *    X ~ X^(0) + X^(1) * X^(2).
 * The first notation is also used by Pitrou et al. 2010, while the second by Beneke
 * et al. 2010 and 2011.
 *
 */

int perturb2_quadratic_sources (
      struct precision * ppr,
      struct precision2 * ppr2,
      struct background * pba,
      struct thermo * pth,            
      struct perturbs * ppt,
      struct perturbs2 * ppt2,
      int index_tau, /**< input: if positive, compute the quadratic sources in
                     ppt->tau_sampling_quadsources[index_tau]; if negative, use
                     interpolation to compute quadratic sources in tau. */
      double tau, /**< input: conformal time where to compute the quadratic sources,
                  using interpolation; ignored if index_tau is positive. */
      int what_to_compute, /**< input: which quadratic sources should we compute? Options are
                           documented in enum quadratic_source_computation */
      double * pvec_quadsources, /**< output: array with the quadratic sources, indexed by
                                 ppw2->index_qs2_XXX */
      double * pvec_quadcollision, /**< output: array with the collisional part of the quadratic
                                 sources, indexed by ppw2->index_qs2_XXX */
      struct perturb2_workspace * ppw2
      )
{
        
  /* Considered wavemodes, and relative shortcuts */
  double k = ppw2->k;
  double k_sq = ppw2->k_sq; 
  double k1 = ppw2->k1;
  double k2 = ppw2->k2;
  double mu = ppw2->cosk1k2;
  double k1_dot_k2 = ppw2->k1_dot_k2;
  double k1_sq = k1*k1;
  double k2_sq = k2*k2;  
  double * k1_m = ppw2->k1_m;
  double * k2_m = ppw2->k2_m;
  double * k1_ten_k2 = ppw2->k1_ten_k2;
  double * k1_ten_k1 = ppw2->k1_ten_k1;
  double * k2_ten_k2 = ppw2->k2_ten_k2;
 
  /* Variables related to the multipole expansion. We use the ppw2->l_max
  which is fixed and does not depend on the approximation scheme. This means
  that we will compute quadratic sources for all multipoles even if the 
  tight coupling approximation is turned on. This part might be optimised
  (TODO). */
  int lm_extra = ppt2->lm_extra;
  int l_max_g = ppw2->l_max_g;
  int l_max_pol_g = ppw2->l_max_pol_g;
  int l_max_ur = ppw2->l_max_ur;
 

  // =====================================================================================
  // =                               Get first order moments                             =
  // =====================================================================================
  
  /* Fill the arrays ppw2->pvec_sources1 and ppw2->pvec_sources2 with the
  first-order perturbations in k1 and k2, respectively. These arrays are
  indexed using the ppt->index_qs_XXX indices and have size equal to
  ppt->qs_size[ppt->index_md_scalars] */
  int qs_size = ppt->qs_size[ppt->index_md_scalars];
  
  /* If the user provides a valid time index (index_tau>=0), use the
  first-order transfer functions in the tabulated values; otherwise interpolate
  them at the desired tau */
  
  
  
  if (index_tau >= 0) { 

    tau = ppt->tau_sampling_quadsources[index_tau];

    if ((ppt2->k3_sampling == sym_k3_sampling) ) {
  		/*here we could improve as there is no tau interpoaltion needed. However nothng is lost apart from speed as the interpoaltion on the node is exact*/
  		class_call (perturb_song_sources_at_tau_and_k (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                k1,
                tau,
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources1),
    		ppt->error_message,
    		ppt2->error_message);
    	
    	class_call (perturb_song_sources_at_tau_and_k (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                k2,
                tau,
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources2),
    		ppt->error_message,
    		ppt2->error_message);
  
 	 	}
  	else {
    
    	for (int index_type=0; index_type<qs_size; ++index_type) {
 
      	/* First-order sources in k1 */
      	ppw2->pvec_sources1[index_type] =
        	ppt->quadsources[ppt->index_md_scalars][ppt2->index_ic_first_order*qs_size + index_type]
                        [index_tau*ppt->k_size[ppt->index_md_scalars]+ppw2->index_k1];
 
      	/* First-order sources in k2 */
      	ppw2->pvec_sources2[index_type] =
        	ppt->quadsources[ppt->index_md_scalars][ppt2->index_ic_first_order*qs_size + index_type]
                        [index_tau*ppt->k_size[ppt->index_md_scalars]+ppw2->index_k2];
    	}
    }
  }
  /* Interpolate first-order sources at the desired time */
  else {
      
    if ((ppt2->k3_sampling == sym_k3_sampling) ) {
  
  		class_call (perturb_song_sources_at_tau_and_k (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                k1,
                tau,
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources1),
    		ppt->error_message,
    		ppt2->error_message);
    	
    		class_call (perturb_song_sources_at_tau_and_k (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                k2,
                tau,
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources2),
    		ppt->error_message,
    		ppt2->error_message);
  
  	}
    else {  
    /* Interpolate first-order quantities in tau and k1 (ppw2->psources_1) */
    	class_call (perturb_song_sources_at_tau (
                 ppr,
                 ppt,
                 ppt->index_md_scalars,
                 ppt2->index_ic_first_order,
                 ppw2->index_k1,
                 tau,
                 ppt->qs_size[ppt->index_md_scalars],
                 ppt->inter_normal,
                 &(ppw2->last_index_sources),
                 ppw2->pvec_sources1),
       	ppt->error_message,
       	ppt2->error_message);
  
    /* Interpolate first-order quantities in tau and k2 (ppw2->psources_2) */ 
    	class_call (perturb_song_sources_at_tau (
                 ppr,
                 ppt,
                 ppt->index_md_scalars,
                 ppt2->index_ic_first_order,
                 ppw2->index_k2,
                 tau,
                 ppt->qs_size[ppt->index_md_scalars],
                 ppt->inter_normal,
                 &(ppw2->last_index_sources),                 
                 ppw2->pvec_sources2),
      	ppt->error_message,
      	ppt2->error_message); 
      	
      }
  }   
      
    /* Interpolate background-related quantities in tau (pvecback) */
    class_call (background_at_tau(
                 pba,
                 tau, 
                 pba->normal_info, 
                 pba->inter_closeby,
                 &(ppw2->last_index_back), 
                 ppw2->pvecback),
      pba->error_message,
      ppt2->error_message);

    /* Interpolate thermodynamics-related quantities in tau (pvecthermo) */
    class_call (thermodynamics_at_z(
                 pba,
                 pth,
                 1./ppw2->pvecback[pba->index_bg_a]-1.,  /* redshift z=1./a-1 */
                 pth->inter_closeby,
                 &(ppw2->last_index_thermo),
                 ppw2->pvecback,
                 ppw2->pvecthermo),
      pth->error_message,
      ppt2->error_message);
      
  

 
  /* Debug - Test the interpolation */
  // if ((ppw2->index_k1==0) && (ppw2->index_k2==0)) {
  //   fprintf (stderr, "%17.7g %17.7g %17.7g %17.7g %17.7g\n",
  //     tau,
  //     a,
  //     Y,
  //     ppw2->pvec_sources1[ppt->index_qs_delta_g],
  //     ppw2->pvec_sources2[ppt->index_qs_delta_g]
  //   );
  // }
 
  /* Define shorthands */
  double * pvec_sources1 = ppw2->pvec_sources1;
  double * pvec_sources2 = ppw2->pvec_sources2;

  /* Shortcuts to background and thermodynamics quantities */
  double * pvecback = ppw2->pvecback;
  double * pvecthermo = ppw2->pvecthermo;
  double * pvecmetric = ppw2->pvecmetric;
  double a = ppw2->pvecback[pba->index_bg_a];
  double a_sq = a*a;
  double Hc = ppw2->pvecback[pba->index_bg_H]*a;
  double kappa_dot = ppw2->pvecthermo[pth->index_th_dkappa];     /* Interaction rate */
  double r = pvecback[pba->index_bg_rho_g]/pvecback[pba->index_bg_rho_b];
  double Y = log10 (a/pba->a_eq);
  
  /* Baryon variables */
  double delta_b_1 = pvec_sources1[ppt->index_qs_delta_b];
  double delta_b_2 = pvec_sources2[ppt->index_qs_delta_b];
  double vpot_b_1 = pvec_sources1[ppt->index_qs_v_b];
  double vpot_b_2 = pvec_sources2[ppt->index_qs_v_b];
  
  /* CDM variables */
  double delta_cdm_1, delta_cdm_2, vpot_cdm_1, vpot_cdm_2;
  if (pba->has_cdm == _TRUE_) {
    delta_cdm_1 = pvec_sources1[ppt->index_qs_delta_cdm];
    delta_cdm_2 = pvec_sources2[ppt->index_qs_delta_cdm];
    if (ppt->gauge != synchronous) {
      vpot_cdm_1 = pvec_sources1[ppt->index_qs_v_cdm];
      vpot_cdm_2 = pvec_sources2[ppt->index_qs_v_cdm];
    }
  }


  if ((what_to_compute == compute_total_and_collision) || (what_to_compute == compute_only_liouville)) {
    
    /* Initialise the quadratic sources vector, as we will increment it
    below with the various contributions */
    for (int index_qs2=0; index_qs2 < ppw2->qs2_size; ++index_qs2)
      pvec_quadsources[index_qs2] = 0;


    // ==============================================================================================
    // =                                   Energy momentum tensor                                   =
    // ==============================================================================================

    /* Since we adopt beta-moments and not fluid variables, the quadratic part of the energy
    momentum tensor is particularly simple. The only quadratic contribution comes from the tetrad
    transformation of T^i0, and is proportional to (psi+phi)*dipole. This is needed only if one
    wants to use the i0 Einstein equation, also known as the longitudinal equation. */

    /* Contribution of the single species */
    double rho_g=0, rho_b=0, rho_cdm=0, rho_ur=0;
    double dipole_b_1=0, dipole_b_2=0;
    double dipole_cdm_1=0, dipole_cdm_2=0;
    double dipole_ur_1=0, dipole_ur_2=0;  

    /* Variables that will accumulate the contribution from all species */
    double rho_dipole_1 = 0;                    /* rho*dipole(k1) */
    double rho_dipole_2 = 0;                    /* rho*dipole(k2) */

 
    /* The only contribution to the quadratic sources is in T^i_0 and we need it to compute the scalar
    potential phi */
    if (ppr2->compute_m[0] == _TRUE_) {
 
      /* Photon contribution */
      rho_g = pvecback[pba->index_bg_rho_g];        
      rho_dipole_1         =  rho_g*I_1_tilde(1);
      rho_dipole_2         =  rho_g*I_2_tilde(1);
 
      /* Baryon contribution */
      rho_b = pvecback[pba->index_bg_rho_b];
      rho_dipole_1         +=  rho_b*pvec_sources1[ppt->index_qs_dipole_b];
      rho_dipole_2         +=  rho_b*pvec_sources2[ppt->index_qs_dipole_b];
 
      /* CDM contribution */
      if (pba->has_cdm == _TRUE_) {
        rho_cdm = pvecback[pba->index_bg_rho_cdm];
        rho_dipole_1         +=  rho_cdm*pvec_sources1[ppt->index_qs_dipole_cdm];
        rho_dipole_2         +=  rho_cdm*pvec_sources2[ppt->index_qs_dipole_cdm];
      }
    
      /* Neutrinos/ur relics contribution */
      if (pba->has_ur == _TRUE_) {
        rho_ur = pvecback[pba->index_bg_rho_ur];
        rho_dipole_1         +=  rho_ur*N_1_tilde(1);
        rho_dipole_2         +=  rho_ur*N_2_tilde(1);
      }
 
    } // end of scalar modes
 


    // ==============================================================================================
    // =                                      Einstein tensor                                       =
    // ==============================================================================================
  
    /* Shortcuts */
    double phi_1, phi_2, psi_1, psi_2, phi_prime_1, phi_prime_2, psi_prime_1, psi_prime_2;
    double eta_1, eta_2, eta_prime_1, eta_prime_2, eta_prime_prime_1, eta_prime_prime_2;
    double h_1, h_2, h_prime_1, h_prime_2, h_prime_prime_1, h_prime_prime_2; 
 
    /* Synchronous gauge */
    if (ppt->gauge == synchronous) {
 
      // *** Define shorthands for the metric variables
    
      /* eta */
      eta_1 =  pvec_sources1[ppt->index_qs_eta];
      eta_2 =  pvec_sources2[ppt->index_qs_eta];
      /* eta' */
      eta_prime_1 =  pvec_sources1[ppt->index_qs_eta_prime];
      eta_prime_2 =  pvec_sources2[ppt->index_qs_eta_prime];
      /* eta'' */
      eta_prime_prime_1 =  pvec_sources1[ppt->index_qs_eta_prime_prime];
      eta_prime_prime_2 =  pvec_sources2[ppt->index_qs_eta_prime_prime];
      /* h */
      h_1 =  pvec_sources1[ppt->index_qs_h];
      h_2 =  pvec_sources2[ppt->index_qs_h];
      /* h' */
      h_prime_1 =  pvec_sources1[ppt->index_qs_h_prime];
      h_prime_2 =  pvec_sources2[ppt->index_qs_h_prime];
      /* h'' */
      h_prime_prime_1 =  pvec_sources1[ppt->index_qs_h_prime_prime];
      h_prime_prime_2 =  pvec_sources2[ppt->index_qs_h_prime_prime];
    
      /* Code the Einstein tensor for synchronous gauge, TODO! */
    
    }
 
 
    /* Newtonian gauge */
    if (ppt->gauge == newtonian) {
 
      /* Define shorthands for the metric variables */
      phi_1 =  pvec_sources1[ppt->index_qs_phi];
      phi_2 =  pvec_sources2[ppt->index_qs_phi];
      psi_1 =  pvec_sources1[ppt->index_qs_psi];
      psi_2 =  pvec_sources2[ppt->index_qs_psi];
      phi_prime_1 = pvec_sources1[ppt->index_qs_phi_prime];
      phi_prime_2 = pvec_sources2[ppt->index_qs_phi_prime];
      if (ppt2->has_isw == _TRUE_) {
        psi_prime_1 = pvec_sources1[ppt->index_qs_psi_prime];
        psi_prime_2 = pvec_sources2[ppt->index_qs_psi_prime];
      }

      /* Scalar potentials */
      if (ppr2->compute_m[0] == _TRUE_) {

        /* Curvature potential (phi_prime), using Poisson equation */
        pvec_quadsources[ppw2->index_qs2_phi_prime_poisson] = 

            (6*psi_1*Hc*(2*psi_2*Hc + phi_prime_2) + 3*phi_prime_1*(2*(-phi_2 + psi_2)*Hc + phi_prime_2)
      
            + phi_1*(-((4*k1_sq + 3*mu*k1*k2 + 4*k2_sq)*phi_2) - 6*Hc*phi_prime_2))/(3.*Hc);

    
        /* Curvature potential (phi_prime), using the Longitudinal (k^i . G^0_i) equation */
        pvec_quadsources[ppw2->index_qs2_phi_prime_longitudinal] = 2 * 1/(2.*k) *
          (
              2 * psi_1 * psi_2 * Hc  * (       k1_m[1]   +     k2_m[1] )
            - 2 * psi_1 * phi_2 * Hc  * (       k1_m[1]                 )
            - 2 * psi_2 * phi_1 * Hc  * (                 +     k2_m[1] )
            - 2 * phi_1 * phi_prime_2 * (       k1_m[1]   +   2*k2_m[1] )
            - 2 * phi_2 * phi_prime_1 * (     2*k1_m[1]   +     k2_m[1] )
            +     psi_1 * phi_prime_2 * (       k1_m[1]                 )
            +     psi_2 * phi_prime_1 * (                       k2_m[1] )
            /* Quadratic term arising from the tetrads */
            + 0.5 * a_sq * ( k1_m[1]/k1*rho_dipole_1*(psi_2+phi_2) + k2_m[1]/k2*rho_dipole_2*(psi_1+phi_1) )
          );
      

        /* Choose between Poisson and longitudinal equations */
        if (ppt2->phi_prime_eq == poisson)
          pvec_quadsources[ppw2->index_qs2_phi_prime] =
            pvec_quadsources[ppw2->index_qs2_phi_prime_poisson];
 
        else if (ppt2->phi_prime_eq == longitudinal)
          pvec_quadsources[ppw2->index_qs2_phi_prime] =
            pvec_quadsources[ppw2->index_qs2_phi_prime_longitudinal];
      

        /* Scalar potential (psi). k1_ten_k2[m+2] picks the 'm' component of the tensorial product between k1 and k2 */    
        pvec_quadsources[ppw2->index_qs2_psi] = - 3./k_sq *
          (
              phi_1 * phi_2 * ( - 3*k1_ten_k2[2] - 2*k1_ten_k1[2] - 2*k2_ten_k2[2] )
            + psi_1 * psi_2 * ( -   k1_ten_k2[2] -   k1_ten_k1[2] -   k2_ten_k2[2] )
            + psi_1 * phi_2 * (     k1_ten_k2[2] +   k1_ten_k1[2]                  )
            + psi_2 * phi_1 * (     k1_ten_k2[2]                  +   k2_ten_k2[2] )
          ); 
      
        /* Conformal time derivative of the above, needed to compute psi_prime */    
        if (ppt2->has_isw == _TRUE_) {
        
          pvec_quadsources[ppw2->index_qs2_psi_prime] = - 3./k_sq *
            (
                (phi_prime_1 * phi_2 + phi_1 * phi_prime_2) * ( - 3*k1_ten_k2[2] - 2*k1_ten_k1[2] - 2*k2_ten_k2[2] )
              + (psi_prime_1 * psi_2 + psi_1 * psi_prime_2) * ( -   k1_ten_k2[2] -   k1_ten_k1[2] -   k2_ten_k2[2] )
              + (psi_prime_1 * phi_2 + psi_1 * phi_prime_2) * (     k1_ten_k2[2] +   k1_ten_k1[2]                  )
              + (psi_prime_2 * phi_1 + psi_2 * phi_prime_1) * (     k1_ten_k2[2]                  +   k2_ten_k2[2] )
            );         
        }
      

      } // end of scalar modes


      /* Vector potentials */
      if (ppr2->compute_m[1] == _TRUE_) {

        /* Vector potential (omega_m1). Note that for k1=k2 and m=1, the quadratic sources for
        the vector potential omega_m1 vanish because k1_ten_k2=0 and k1_ten_k1=-k2_ten_k2 */
        pvec_quadsources[ppw2->index_qs2_omega_m1_prime] = 2. * sqrt_3 / k * 
          (
              phi_1 * phi_2 * ( - 3*k1_ten_k2[3] - 2*k1_ten_k1[3] - 2*k2_ten_k2[3] )
            + psi_1 * psi_2 * ( -   k1_ten_k2[3] -   k1_ten_k1[3] -   k2_ten_k2[3] )
            + psi_1 * phi_2 * (     k1_ten_k2[3] +   k1_ten_k1[3]                  )
            + psi_2 * phi_1 * (     k1_ten_k2[3]                  +   k2_ten_k2[3] )
          );

      } // end of vector modes


      /* Tensor potentials */
      if (ppr2->compute_m[2] == _TRUE_) {

        /* Tensor potential (gamma_m2) sources */
        pvec_quadsources[ppw2->index_qs2_gamma_m2_prime_prime] = -2 *
          (
              phi_1 * phi_2 * ( - 3*k1_ten_k2[4] - 2*k1_ten_k1[4] - 2*k2_ten_k2[4] )
            + psi_1 * psi_2 * ( -   k1_ten_k2[4] -   k1_ten_k1[4] -   k2_ten_k2[4] )
            + psi_1 * phi_2 * (     k1_ten_k2[4] +   k1_ten_k1[4]                  )
            + psi_2 * phi_1 * (     k1_ten_k2[4]                  +   k2_ten_k2[4] )
          );

      } // end of tensor modes

    } // end of if(newtonian)
 
 

    // ==========================================================================================
    // =                                   Liouville operator                                   =
    // ==========================================================================================
 
    if (ppt2->has_quadratic_liouville == _TRUE_) {
 
      /* Newtonian gauge */
      if (ppt->gauge == newtonian) {
 
        // ------------------------------------------------
        // -               Photon temperature             -
        // ------------------------------------------------

        for (int l=0; l<=l_max_g; ++l) {      
          for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
          
            int m = ppt2->m[index_m];

            /* Monopole-specific contribution */
            if (l==0)
              dI_qs2(0,m) = - 8 * (phi_1*phi_prime_2 + phi_2*phi_prime_1);
      
            /* Dipole-specific contribution */
            if (l==1)
              dI_qs2(1,m) = 4 * (k1_m[m+1]*psi_1*(psi_2-phi_2) + k2_m[m+1]*psi_2*(psi_1-phi_1));
          
            /* Time-delay, redshift and lensing contributions. Here we write down the 3rd and 4th row of
            eq. 143 of BF2010, equivalent to eqs. A.43 and A.44 of P2010. The coupling coefficients R are
            basically equal to l*C, hence the associated terms are important also for very high l's. All
            the R-terms come from the lensing part of Boltzmann equation, and are difficult to treat
            in the line-of-sight formalism. */
            dI_qs2(l,m) +=   I_2_tilde(l+1) * c_plus_22(l,m)*k2*(phi_1+psi_1)
                           - I_2_tilde(l-1) * c_minus_22(l,m)*k2*(phi_1+psi_1)
                           /* Symmetrisation */
                           + I_1_tilde(l+1) * c_plus_11(l,m)*k1*(phi_2+psi_2)
                           - I_1_tilde(l-1) * c_minus_11(l,m)*k1*(phi_2+psi_2);
     
            dI_qs2(l,m) += - 4*phi_prime_1*I_2(l,m)
                           + I_2_tilde(l+1) * 4 * c_plus_12(l,m)*k1*psi_1
                           - I_2_tilde(l-1) * 4 * c_minus_12(l,m)*k1*psi_1
                           /* Symmetrisation */
                           - 4*phi_prime_2*I_1(l,m)
                           + I_1_tilde(l+1) * 4 * c_plus_21(l,m)*k2*psi_2
                           - I_1_tilde(l-1) * 4 * c_minus_21(l,m)*k2*psi_2;

            dI_qs2(l,m) +=   I_2_tilde(l+1) * r_plus_12(l,m)*k1*(phi_1+psi_1)
                           - I_2_tilde(l-1) * r_minus_12(l,m)*k1*(phi_1+psi_1)
                           /* Symmetrisation */
                           + I_1_tilde(l+1) * r_plus_21(l,m)*k2*(phi_2+psi_2)
                           - I_1_tilde(l-1) * r_minus_21(l,m)*k2*(phi_2+psi_2);

            /* Uncomment the following lines to include all effects nonetheless */
            // dI_qs2(l,m) += - 4*phi_prime_1*I_2(l,m) 
            //   + I_2_tilde(l+1) * ( r_plus_12(l,m)*k1*(phi_1+psi_1)
            //     + c_plus_22(l,m)*k2*(phi_1+psi_1)  +  4*c_plus_12(l,m)*k1*psi_1  )
            //   - I_2_tilde(l-1) * ( r_minus_12(l,m)*k1*(phi_1+psi_1)
            //     + c_minus_22(l,m)*k2*(phi_1+psi_1) +  4*c_minus_12(l,m)*k1*psi_1 );
            // /* Symmetrisation */
            // dI_qs2(l,m) += - 4*phi_prime_2*I_1(l,m)
            //   + I_1_tilde(l+1) * ( r_plus_21(l,m)*k2*(phi_2+psi_2) 
            //     + c_plus_11(l,m)*k1*(phi_2+psi_2)  +  4*c_plus_21(l,m)*k2*psi_2  )
            //   - I_1_tilde(l-1) * ( r_minus_21(l,m)*k2*(phi_2+psi_2) 
            //     + c_minus_11(l,m)*k1*(phi_2+psi_2) +  4*c_minus_21(l,m)*k2*psi_2 );

            /* Account for the fact that in BF2010 the Liouville operator appears on the left-hand-side */
            dI_qs2(l,m) *= -1;
      
          } // end of for (index_m)
        } // end of for (l)

   
        // ----------------------------------------
        // -          Photon polarization         -
        // ----------------------------------------

        if (ppt2->has_polarization2 == _TRUE_) {
 
          for (int l=2; l<=l_max_pol_g; ++l) {      
            for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
          
              int m = ppt2->m[index_m];

              /* E-modes. Second and third row of eq. 145 of BF2010, without the first-order B-modes. */
              dE_qs2(l,m)  = - 4*phi_prime_1*E_2(l,m) 
                             + E_2_tilde(l+1) * ( k_plus_12(l,m)*k1*(phi_1+psi_1)
                               + d_plus_22(l,m)*k2*(phi_1+psi_1)  +  4*d_plus_12(l,m)*k1*psi_1  )
                             - E_2_tilde(l-1) * ( k_minus_12(l,m)*k1*(phi_1+psi_1)
                               + d_minus_22(l,m)*k2*(phi_1+psi_1) +  4*d_minus_12(l,m)*k1*psi_1 )
                             /* Symmetrisation */
                             - 4*phi_prime_2*E_1(l,m)
                             + E_1_tilde(l+1) * ( k_plus_21(l,m)*k2*(phi_2+psi_2)
                               + d_plus_11(l,m)*k1*(phi_2+psi_2)  +  4*d_plus_21(l,m)*k2*psi_2  )
                             - E_1_tilde(l-1) * ( k_minus_21(l,m)*k2*(phi_2+psi_2)
                               + d_minus_11(l,m)*k1*(phi_2+psi_2) +  4*d_minus_21(l,m)*k2*psi_2 );
           
              /* B-modes. Fourth and fifth row of eq. 146 of BF2010 (the other rows contain first-order B-modes) */
              dB_qs2(l,m)  = - E_2_tilde(l) * ( k_zero_12(l,m)*k1*(phi_1+psi_1) + d_zero_22(l,m)*k2*(phi_1+psi_1)
                               + 4*d_zero_12(l,m)*k1*psi_1 )
                             /* Symmetrisation */
                             - E_1_tilde(l) * ( k_zero_21(l,m)*k2*(phi_2+psi_2) + d_zero_11(l,m)*k1*(phi_2+psi_2)
                               + 4*d_zero_21(l,m)*k2*psi_2 );

              /* Account for the fact that in BF2010 the Liouville operator appears on the left-hand-side */
              dE_qs2(l,m) *= -1;
              dB_qs2(l,m) *= -1;
 
            } // end of for (index_m)
          } // end of for (l)
        } // end of if(has_polarization2)
 
 
        // ---------------------------------------
        // -                Baryons              -
        // ---------------------------------------
 
        /* Baryon monopole */
        if (ppr2->compute_m[0] == _TRUE_)
          db_qs2(0,0,0) = 
              2*k1_dot_k2*(psi_1-phi_1)*vpot_b_2 + k2_sq*(phi_1+psi_1)*vpot_b_2 + 3*phi_prime_1*(delta_b_2 + 2*phi_2)
            + 2*k1_dot_k2*(psi_2-phi_2)*vpot_b_1 + k1_sq*(phi_2+psi_2)*vpot_b_1 + 3*phi_prime_2*(delta_b_1 + 2*phi_1);
 
 
        /* Baryon dipole */
        for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {

          int m = ppt2->m[index_m];

          db_qs2(1,1,m) =   3*k1_m[m+1]*(delta_b_2*psi_1 - 4*vpot_b_1*phi_prime_2 + phi_2*psi_1)
                          + 3*k2_m[m+1]*(delta_b_1*psi_2 - 4*vpot_b_2*phi_prime_1 + phi_1*psi_2)
                          - 3*psi_1*psi_2*(k1_m[m+1] + k2_m[m+1]);
        }
   
 
        /* Baryon pressure & quadrupole */
        if (ppt2->has_perfect_baryons == _FALSE_) {
        
          if (ppr2->compute_m[0] == _TRUE_)
            db_qs2(2,0,0) = 2*k1_dot_k2*(psi_1*vpot_b_2 + psi_2*vpot_b_1);
 
          for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
            int m = ppt2->m[index_m];
            db_qs2(2,2,m) = -15*ppw2->k1_ten_k2[m+2]*(psi_1*vpot_b_2 + psi_2*vpot_b_1);
          }
        }


        // ----------------------------------------------
        // -               Cold Dark Matter             -
        // ----------------------------------------------

 
        if (pba->has_cdm == _TRUE_) {     
 
          /* CDM monopole */
          if (ppr2->compute_m[0] == _TRUE_)
            dcdm_qs2(0,0,0) = 
                2*k1_dot_k2*(psi_1-phi_1)*vpot_cdm_2 + k2_sq*(phi_1+psi_1)*vpot_cdm_2 + 3*phi_prime_1*(delta_cdm_2 + 2*phi_2)
              + 2*k1_dot_k2*(psi_2-phi_2)*vpot_cdm_1 + k1_sq*(phi_2+psi_2)*vpot_cdm_1 + 3*phi_prime_2*(delta_cdm_1 + 2*phi_1);
 
 
          /* CDM dipole */
          /* Note that the last contribution vanishes for m=1 since k1_[1] + k2_[1] = 0. */
          for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
            int m = ppt2->m[index_m];
            dcdm_qs2(1,1,m) =   3*k1_m[m+1]*(delta_cdm_2*psi_1 - 4*vpot_cdm_1*phi_prime_2 + phi_2*psi_1)
                              + 3*k2_m[m+1]*(delta_cdm_1*psi_2 - 4*vpot_cdm_2*phi_prime_1 + phi_1*psi_2)
                              - 3*psi_1*psi_2*(k1_m[m+1] + k2_m[m+1]);
          }
 
 
          /* CDM pressure & quadrupole */
          if (ppt2->has_perfect_cdm == _FALSE_) {

            if (ppr2->compute_m[0] == _TRUE_)
              dcdm_qs2(2,0,0) = 2*k1_dot_k2*(psi_1*vpot_cdm_2 + psi_2*vpot_cdm_1);
 
            for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
              int m = ppr2->m[index_m];
              dcdm_qs2(2,2,m) = -15*ppw2->k1_ten_k2[m+2]*(psi_1*vpot_cdm_2 + psi_2*vpot_cdm_1);
            }
          }
       
        } // end of if(has_cdm)
 
 
        // -------------------------------------------------------
        // -              Ultra Relativistic Neutrinos           -
        // -------------------------------------------------------
 
        /* This is exactly the same hierarchy as the one above for the photons. */
        if (pba->has_ur == _TRUE_) {
        
          for (int l=0; l<=l_max_ur; ++l) {      
            for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

              int m = ppt2->m[index_m];

              /* Monopole-specific contribution */
              if (l==0)
                dN_qs2(0,m) = - 8*phi_1*phi_prime_2 - 8*phi_2*phi_prime_1;
      
      
              /* Dipole-specific contribution */
              if (l==1)
                dN_qs2(1,m) = 4 * (k1_m[m+1]*psi_1*(psi_2-phi_2) + k2_m[m+1]*psi_2*(psi_1-phi_1));
          
 
              /* Time-delay, lensing and redshift contributions */
              dN_qs2(l,m) += - 4*phi_prime_1*N_2(l,m) 
                             + N_2_tilde(l+1) * ( r_plus_12(l,m)*k1*(phi_1+psi_1)
                             + c_plus_22(l,m)*k2*(phi_1+psi_1) + 4*c_plus_12(l,m)*k1*psi_1  )
                             - N_2_tilde(l-1) * ( r_minus_12(l,m)*k1*(phi_1+psi_1) 
                             + c_minus_22(l,m)*k2*(phi_1+psi_1) + 4*c_minus_12(l,m)*k1*psi_1 )
                             /* Symmetrisation */
                             - 4*phi_prime_2*N_1(l,m)
                             + N_1_tilde(l+1) * ( r_plus_21(l,m)*k2*(phi_2+psi_2) 
                             + c_plus_11(l,m)*k1*(phi_2+psi_2) + 4*c_plus_21(l,m)*k2*psi_2  )
                             - N_1_tilde(l-1) * ( r_minus_21(l,m)*k2*(phi_2+psi_2)
                             + c_minus_11(l,m)*k1*(phi_2+psi_2) + 4*c_minus_21(l,m)*k2*psi_2 );
       
      
              /* Account for the fact that in BF2010 the Liouville operator appears on the left-hand-side */
              dN_qs2(l,m) *= -1;
      
            } // end of for (index_m)
          } // end of for (l)
        } // end of if(has_ur) 
      
      }  // end of if(newtonian)
 
    } // end of if(has_quadratic_liouville)
  
  } // end of if (compute_total_and_collision)


  if ((what_to_compute == compute_total_and_collision) || (what_to_compute == compute_only_collision)) {

    // ==========================================================================================
    // =                                    Collision term                                      =
    // ==========================================================================================
 
    /* We now compute the quadratic part of the collision term for the various species. As in
    first order, the collision term is multiplied by the Compton scattering rate, kappa_dot.

    We shall write down the quadratic part of the collision term following
    http://arxiv.org/abs/1405.2280; in particular, we use eq. 4.151 for the intensity,
    eq. 4.154 for the E-mode polarisation and eq. 4.157 for the B-mode polarisation.
    The same equations were derived by Beneke, Fidler & Klingmuller 2011 and match those
    in Pitrou, Uzan and Bernardeau 2010.
    
    Regardless of whether we deal with intensity or polarisation, the quadratic collision term
    has two contributions. The first involves the collision term at first order C_lm and has
    the form (A+delta_e)*C_lm, where A is the time potential in Newtonian gauge.
  
    The second contribution consists of several terms all proportional to the free electron
    velocity. Each of these terms involves a sum over the neighbouring moments l+-1 and m+-1
    weighted by the coupling coefficients C and D (for details on the coefficients, see sec.
    A.4.1 of http://arxiv.org/abs/1405.2280). We implement the sum over m directly in the 
    definition of the coupling coefficients, which we compute in the function
    perturb2_geometrical_corner(). */
     
    for (int index_qs2=0; index_qs2 < ppw2->qs2_size; ++index_qs2)
      pvec_quadcollision[index_qs2] = 0;
 
    if (ppt2->has_quadratic_collision == _TRUE_) {
     
      /* Spherical components of the linear baryon velocity; obtained as 
          u_m(k1) = -k1_m * vpot(k1) = rot_1(1,m) * v_0_1
          u_m(k2) = -k2_m * vpot(k2) = rot_1(1,m) * v_0_1,
      where vpot is the scalar potential of the irrotation velocity, v^i = dv/dx^i,
      and v_0_1 (k1) is just vpot_b_1 or vpot_b_2 */
      double u_b_1[2], u_b_2[2];
      for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
        int m = ppt2->m[index_m];
        u_b_1[m] = -ppw2->k1_m[m+1] * vpot_b_1;
        u_b_2[m] = -ppw2->k2_m[m+1] * vpot_b_2;
      }


      /* The collision term can be written in a slightly more compact form by 
      employing the v_0 variable of Pitrou et al. 2010, eq. A.38. This variable is
      the scalar part of the velocity at first-order, if the zenith is aligned with k.
      We omit the b symbol to allievate the notation of the collision term. */
      double v_0_1 = -k1*vpot_b_1;
      double v_0_2 = -k2*vpot_b_2;


      /* The collision term doesn't involve metric variables, because it is computed
      in the tetrad gauge frame, where Minkowski reigns.
      However, if we use conformal time, the collision term appears on the right hand
      side of Boltzmann equation divided by the photon energy in the gauge frame, P_0.
      The metric terms creep in when expressing P_0 in terms of tetrad frame variables.
      We call such contribution A.  In Newtonian gauge, A is simply the Newtonian potential,
      as can be seen in eq. A.55 of Pitrou et al. 2010 and eq. 2.18 of Beneke and Fidler
      2011. In synchronous gauge P_0 = E, where E is the tetrad frame energy, and thus
      we can set A to zero. */
      double A_1=0., A_2=0.;    
      if(ppt->gauge == newtonian) {
        A_1 = pvec_sources1[ppt->index_qs_psi];
        A_2 = pvec_sources2[ppt->index_qs_psi];        
      }
  
      /* First order collision term in k1 and k2 */
      double c_1=0, c_2=0;
  

      // -------------------------------------------------------------
      // -                   Perturbed recombination                 -
      // -------------------------------------------------------------
    
      /* 'delta_e' is the density contrast of free electrons, which we assume to
      be equal to 'delta_b' if perturbed recombination is not requested. */  
      double delta_e_1 = delta_b_1;
      double delta_e_2 = delta_b_2;

      if (ppt2->has_perturbed_recombination_stz == _TRUE_) {
 
        /* If requested, we use the approximated formula in 3.23 of Senatore, Tassev, Zaldarriaga (STZ,
        http://arxiv.org/abs/0812.3652), which is valid for k < 1 in Newtonian gauge and it doesn't require
        to solve differential equations. */
        if (ppt2->perturbed_recombination_use_approx == _TRUE_) {

          double xe = pvecthermo[pth->index_th_xe];
          double xe_dot = pvecthermo[pth->index_th_dxe];
          delta_e_1 = delta_b_1 * (1. - 1/(3*Hc)*(xe_dot/xe));
          delta_e_2 = delta_b_2 * (1. - 1/(3*Hc)*(xe_dot/xe));
        }
        /* Full formalism, which involves computing the derivatives of the Q function (see STZ of appendix A
        of Pitrou et al. 2010) and solving an additional equation for delta_Xe in the first-order module. */
        else {
        
          delta_e_1 = delta_b_1 + pvec_sources1[ppt->index_qs_delta_Xe];
          delta_e_2 = delta_b_2 + pvec_sources2[ppt->index_qs_delta_Xe];
        }

        /* Some debug. Make sure that pth->compute_xe_derivatives = _TRUE_ */
        // thermodynamics_at_z(pba,pth,1/a-1,pth->inter_normal,&(ppw2->last_index_thermo),
        //   ppw2->pvecback,ppw2->pvecthermo);
        // 
        // double kappa_dot = pvecthermo[pth->index_th_dkappa];
        // double exp_minus_kappa = pvecthermo[pth->index_th_exp_m_kappa];
        // double g = pvecthermo[pth->index_th_g];
        // double xe = pvecthermo[pth->index_th_xe];
        // double xe_dot = pvecthermo[pth->index_th_dxe];
        // double approx = delta_b_1 * (1. - 1/(3*Hc)*(xe_dot/xe));
        // 
        // if ((ppw2->index_k1==0) && (ppw2->index_k2==0) && (ppw2->index_k3==0))
        //   fprintf (stderr, "%12.7g %12.7g %12.7g %12.7g %12.7g %12.7g\n",
        //     ppt->tau_sampling_quadsources, a, delta_e_1, approx, delta_b_1, g);

      } // end of if(has_perturbed_recombination_stz)
 
  
      // ------------------------------------------------
      // -               Photon temperature             -
      // ------------------------------------------------
  
      // *** Monopole
 
      /* The monopole has a particularly simple form.  We write it down explicitely
      as it is also more stable numerically when k1=k2.
      The monopole term is tight-coupling suppressed, as it contains the difference
      between the photon and baryon velocities at first order, which are almost equal.
      This cancellation leads to a numerical instability, which could be solved by using
      the tight coupling approximation at first order, and by storing v_b - v_g in the
      ppt->quadsources array.  However, if I use the TCA at first order, I get a wierd shape
      for the shear_g at first order, and in general the whole second-order system is
      slowed down.  Hence, the best strategy so far is to just avoid the TCA at first
      order and evolve the first-order system with high precision by lowering
      the parameter tol_perturb_integration (1e-10 works perfectly, even though already
      with 1e-5 the results are good). */

      if (ppr2->compute_m[0] == _TRUE_) {
        double vpot_g_1    =   ppw2->pvec_sources1[ppt->index_qs_v_g];
        double vpot_g_2    =   ppw2->pvec_sources2[ppt->index_qs_v_g];

        dI_qc2(0,0) = four_thirds * ppw2->k1_dot_k2 * (vpot_b_1*(vpot_g_2 - vpot_b_2) + vpot_b_2*(vpot_g_1 - vpot_b_1));
      }

      // *** All other moments
      for (int l=1; l<=l_max_g; ++l) {      
        for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

          int m = ppt2->m[index_m];
  
          // *** Dipole
          if (l==1) {
  
            c_1 = 4*u_b_1[m] - I_1(1,m);
            c_2 = 4*u_b_2[m] - I_2(1,m);
 
            dI_qc2(1,m) =  3 * ( c_minus_12(1,m) * v_0_1 * I_2_tilde(0)
                               + c_minus_21(1,m) * v_0_2 * I_1_tilde(0));
                               

            /* In perturb2_derivs(), we have written the purely second-order part of
            the dipole equation as
                collision = kappa_dot * (4/3*b(1,1,m) - I(1,m)) + quadcollision_song.
            while in my thesis (eq. 4.144 and 4.151 of http://arxiv.org/abs/1405.2280)
            it is expressed using the baryon velocity:
                collision = kappa_dot * (4*v_b[m] - I(1,m)) + quadcollision_thesis.
            In order to be consistent, here we need to build the quadratic part
            of the collision term that matches quadcollision_song; so far, however,
            we have built quadcollision_thesis using eq. 4.151 of my thesis.
            The former can be obtained from the latter after using the relation between
            the baryon dipole and its velocity in eq. 4.46,
                v_b[m] = b(1,1,m)/3 - delta*v_b[m],
            which yields
                quadcollision_song = quadcollision_thesis - 4*delta_b*v_b[m].
            Therefore, we need to subtract from dI_qc2(1,m)=quadcollision_thesis the
            factor 4*delta_b*v_b[m]. If we omit doing so, we will not be able to
            reproduce the tight coupling regime, whereby v_g=v_b before recombination. */
            dI_qc2(1,m) += -4 * (delta_b_1*u_b_2[m] + delta_b_2*u_b_1[m]);

          }



          // *** Quadrupole
          if (l==2) {
          
            c_1 = - I_1(2,m) + 0.1*(I_1(2,m) - sqrt_6*E_1(2,m));
            c_2 = - I_2(2,m) + 0.1*(I_2(2,m) - sqrt_6*E_2(2,m));
 
            dI_qc2(2,m) =   c_minus_12(2,m) * v_0_1 * (7*v_0_2 - 0.5*I_2_tilde(1))
                          + c_minus_21(2,m) * v_0_2 * (7*v_0_1 - 0.5*I_1_tilde(1));
          }
  
  
          // *** Octupole
          if (l==3) {
            
            c_1 = - I_1(3,m);
            c_2 = - I_2(3,m);
        
            dI_qc2(3,m) =   0.5 * (  c_minus_12(3,m) * v_0_1 * (I_2_tilde(2) - sqrt_6*E_2_tilde(2))
                                   + c_minus_21(3,m) * v_0_2 * (I_1_tilde(2) - sqrt_6*E_1_tilde(2)));
          }
          
  
  
          // *** Higher moments
          if (l>3) {
            
            c_1 = - I_1(l,m);
            c_2 = - I_2(l,m);

          }
        
        
          // *** All moments
        
          /* First order collision term contribution, plus the fifth line of equation 2.18. This
          is the same for every l-moment. In the tight coupling regime, this contribution should
          be O(1) for l>=3 because all first-order multipoles with l>=2 are strongly suppressed,
          while the contributions to the dipole and the quadrupole are of order O(kappa_dot). */
          dI_qc2(l,m) +=  (A_1 + delta_e_1)*c_2  +  (A_2 + delta_e_2)*c_1
                        + c_minus_12(l,m) * v_0_1 * I_2_tilde(l-1)
                        - c_plus_12(l,m)  * v_0_1 * I_2_tilde(l+1)
                        /* Symmetrisation */
                        + c_minus_21(l,m) * v_0_2 * I_1_tilde(l-1)
                        - c_plus_21(l,m)  * v_0_2 * I_1_tilde(l+1);
 
        } // end of for (index_m)
      } // end of for (l)
 
 
 
 
      // *** A note on  1/2 factors and symmetrization
    
      /* We wrote down the collision term using the equations given in Beneke & Fidler 2011.
      They use the convention whereby the perturbative expansion of the variable X is
      X=X^0 + X^1 +  X^2, while we use X=X^0 + X^1 +  1/2*X^2.  Hence, we should include
      a 2 factor when we add the quadratic collision term to the other quadratic sources.
      However, when we symmetrised the collision term wrt k1<->k2 we did not include
      the 1/2 factor; by doing so, we already implicitely included the 2 factor, hence
      we do not need to do it now.  */
  
 
      // -------------------------------------------
      // -        Photon E-mode polarization       -
      // -------------------------------------------
 
      if (ppt2->has_polarization2 == _TRUE_) {
 
        /* We write down the collision term for E-mode polarization from eq. 2.19 of BFK 2011.
        Note that, since for E-mode polarization there is no monopole nor dipole, we start
        from the quadrupole (l=2). */
  
        // ** Moments with l>=2
        for (int l=2; l<=l_max_pol_g; ++l) {
          for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
        
            int m = ppt2->m[index_m];
          
            // *** Quadrupole
            if (l==2) {
          
              c_1 = - E_1(2,m) - sqrt_6 * 0.1 * (I_1(2,m) - sqrt_6*E_1(2,m));
              c_2 = - E_2(2,m) - sqrt_6 * 0.1 * (I_2(2,m) - sqrt_6*E_2(2,m));
          
              dE_qc2(2,m) =   0.5 * sqrt_6 *
                             (  c_minus_12(2,m) * v_0_1 * (I_2_tilde(1) - 2*v_0_2)
                              + c_minus_21(2,m) * v_0_2 * (I_1_tilde(1) - 2*v_0_1));
            }
         
          
            // *** Octupole
            if (l==3) {
            
              c_1 = - E_1(3,m);
              c_2 = - E_2(3,m);
 
              dE_qc2(3,m) = - 0.5 * sqrt_6 *
                             (  d_minus_12(3,m) * v_0_1 * (I_2_tilde(2) - sqrt_6*E_2_tilde(2))
                              + d_minus_21(3,m) * v_0_2 * (I_1_tilde(2) - sqrt_6*E_1_tilde(2)));
            }
          
          
            // *** Higher moments
            if (l>3) {
            
              c_1 = - E_1(l,m);
              c_2 = - E_2(l,m);
            
            }
      

            // *** All moments

            /* First-order collision term, plus fourth line of equation 2.19 */
            dE_qc2(l,m) += (A_1 + delta_e_1)*c_2  +  (A_2 + delta_e_2)*c_1
                           + d_minus_12(l,m) * v_0_1 * E_2_tilde(l-1)
                           - d_plus_12(l,m)  * v_0_1 * E_2_tilde(l+1)
                           /* Symmetrisation */
                           + d_minus_21(l,m) * v_0_2 * E_1_tilde(l-1)
                           - d_plus_21(l,m)  * v_0_2 * E_1_tilde(l+1);

      
          } // end of for (index_m)
        } // end of for (l)
      
 
        // -------------------------------------------
        // -        Photon B-mode polarization       -
        // -------------------------------------------
 
        /* We write down the collision term for B-mode polarization from eq. 2.20 of BFK 2011.
        Note that, since for B-mode polarization there is no monopole nor dipole, we start
        from the quadrupole (l=2). */
  
        // ** Moments with l>=2
        for (int l=2; l<=l_max_pol_g; ++l) {      
          for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

            int m = ppt2->m[index_m];
          
            // *** Quadrupole
            if (l==2)
              dB_qc2(2,m) = - 0.2*sqrt_6 *
                             (  d_zero_12(2,m) * v_0_1 * (I_2_tilde(2) - sqrt_6*E_2_tilde(2))
                              + d_zero_21(2,m) * v_0_2 * (I_1_tilde(2) - sqrt_6*E_1_tilde(2)));
 
            /* Second line of equation 2.20 */
            dB_qc2(l,m) +=    d_zero_12(l,m) * v_0_1 * E_2_tilde(l)
                            + d_zero_21(l,m) * v_0_2 * E_1_tilde(l);

          
          } // end of for (index_m)
        } // end of for (l)
 
      } // end of if(has_polarization2)
 
 
      // ---------------------------------------
      // -                Baryons              -
      // ---------------------------------------
 
      /* The collision term for the baryon monopole and dipole is the
      the photon's multiplied by -r = -rho_g/rho_b. See comment in 
      perturb2_derivs() or eq. 5.16 of http://arxiv.org/abs/1405.2280
      for details. */
 
      /* Monopole collision term */
      if (ppr2->compute_m[0] == _TRUE_)
        db_qc2(0,0,0) = - r * dI_qc2(0,0);
 
      /* Dipole collision term */
      for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m)
        db_qc2(1,1,ppt2->m[index_m]) = - r * dI_qc2(1,ppt2->m[index_m]);
          
 
      /* Pressure and quadrupole collision term */
      if (ppt2->has_perfect_baryons == _FALSE_) {
 
        /* TODO: Derive baryon pressure & quadrupole collision terms!!!!
        For now, we set them to zero */
 
      }


		// ----------------------------------------------
    // -               magnetic fields              -
    // ----------------------------------------------
    
    //  Note that we have put the magnetic field here. It is not a collision term in the classical sense, but the origin of the magnetic field is directly linked to collisions.  
 		if (ppt2->has_magnetic_field == _TRUE_) {
 			for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
        int m = ppt2->m[index_m];
        
        c_1 = 4*u_b_1[m] - I_1(1,m);
        c_2 = 4*u_b_2[m] - I_2(1,m);
        double D_1 = -pvec_sources1[ppt->index_qs_phi];
      	double D_2 = -pvec_sources2[ppt->index_qs_phi];
      	double delta_g_1 = pvec_sources1[ppt->index_qs_delta_g];
  			double delta_g_2 = pvec_sources2[ppt->index_qs_delta_g];
        //factor sigma_t/e missing + remove delta_e C1 and add -Psi C1
        dmag_qs2(1,m) = + k* pvecback[pba->index_bg_rho_g] /3.*(
          		 dI_qc2(1,ppt2->m[index_m])
          		 
          		// here we substract delta_e C1 and add -Psi C1.
          			-(delta_e_1 - D_1)*c_2  -  (delta_e_2 - D_2)*c_1
          			
          		// test source term
          		//( A_1 + D_1 + delta_g_1 )*c_2  +  (A_2 + D_2 + delta_g_2)*c_1
          			);
          			
          			//old code
          			//slip
          		 //	+( I_1(0,0) +phi_1 - psi_1) * ( I_2(1,m)/4. - (-k2_m[m+1] * v_b_2))
          			//	+( I_2(0,0) +phi_2 - psi_2) * ( I_1(1,m)/4. - (-k1_m[m+1] * v_b_1))
          				
          				// contribution from second order slip
          			//	- I_1(0,0)* I_2(1,m)/4. + delta_b_1 * (-k2_m[m+1]) * v_b_2
          			//	- I_2(0,0)* I_1(1,m)/4.+ delta_b_2 * (-k1_m[m+1]) * v_b_1
          				
          				// anisotropic stree
          				
          			//	- sqrt((2.-m)*(3.-m)/2.) *(I_1(2,m-1) * k2_m[1+1] * v_b_2)
          			//	+ sqrt(4.-m*m) *(I_1(2,m) * k2_m[1] * v_b_2)
        				//	- sqrt((2.+m)*(3.+m)/2.) *(I_1(2,m+1) * k2_m[1-1] * v_b_2)
        				//	- sqrt((2.-m)*(3.-m)/2.) *(I_2(2,m-1) * k1_m[1+1] * v_b_1)
          			//	+ sqrt(4.-m*m) *(I_2(2,m) * k1_m[1] * v_b_1)
        				//	- sqrt((2.+m)*(3.+m)/2.) *(I_2(2,m+1) * k1_m[1-1] * v_b_1)
        					
          	
          
     	 }
			}


      // ---------------------------------------------------------------------
      // -                      Add collisions to sources                    -
      // ---------------------------------------------------------------------


      for (int index_qs2=0; index_qs2 < ppw2->qs2_size; ++index_qs2) {
        pvec_quadcollision[index_qs2] *= kappa_dot;
        if (what_to_compute == compute_total_and_collision)
          pvec_quadsources[index_qs2] += pvec_quadcollision[index_qs2];
      }

    }  // end of if(has_quadratic_collision)
  } // end of if(compute_only_collision)  



  // =======================================================================================
  // =                                  Other quadratic sources                            =
  // =======================================================================================

  /* Store delta*delta and velocity*velocity terms */
  if (ppt2->has_perfect_baryons == _TRUE_) {
    pvec_quadsources[ppw2->index_qs2_dd_b] = delta_b_1 * delta_b_2;
    pvec_quadsources[ppw2->index_qs2_vv_b] = vpot_b_1 * vpot_b_2;
  }

  if (pba->has_cdm == _TRUE_)
    if (ppt2->has_perfect_cdm == _TRUE_)
      pvec_quadsources[ppw2->index_qs2_vv_cdm] = vpot_cdm_1 * vpot_cdm_2;

  return _SUCCESS_; 
        
}











/**
 * Build the second-order line-of-sight sources and store them in ppt2->sources.
 *
 * This function is called from inside the differential solver, so that it can
 * access all perturbations as they are evolved.
 *
 * The derivatives of the perturbations need to be written with respect to conformal time
 * (tau). You can find find a summary of all equations involved in sec. 5.3.1 
 * http://arxiv.org/abs/1405.2280. More technical details on the differential system
 * and on the solver can be found in sec 5.3.2 and 5.3.3.
 *
 * This function is never called explicitly in this module. Instead, it is passed as an
 * argument to the evolver, which calls it whenever it hits a time contained in the time
 * sampling array ppt2->tau_sampling. Therefore, this function is called ppt2->tau_size
 * times for each (k1,k2,k3) triplet.
 *
 * Since the evolver should work with functions passed
 * from various modules, the format of the arguments is a bit special:
 * - fixed parameters and workspaces are passed through the generic pointer.
 *   parameters_and_workspace.
 * - errors are not written as usual in ppt2->error_message, but in a generic
 *   error_message passed in the list of arguments.
 */

int perturb2_sources (
      double tau, /**< current time */
      double * y, /**< values of evolved perturbations at tau (y[index_pt2_XXX]) */
      double * dy, /**< output: derivatives of evolved perturbations at tau (dy[index_pt2_XXX]) */
      int index_tau, /**< current time index (from ppt2->tau_sampling array) */
      void * parameters_and_workspace, /**< generic structure with all needed parameters, including
                                       backgroudn and thermo structures; will be cast to type
                                       perturb2_parameters_and_workspace()  */
      ErrorMsg error_message /**< error message */
      )
{

  /* Shortcuts for the structures */
  struct perturb2_parameters_and_workspace * pppaw2 = parameters_and_workspace;
  struct precision * ppr = pppaw2->ppr;
  struct precision2 * ppr2 = pppaw2->ppr2;
  struct background * pba = pppaw2->pba;
  struct thermo * pth = pppaw2->pth;
  struct perturbs * ppt = pppaw2->ppt;
  struct perturbs2 * ppt2 = pppaw2->ppt2;  
  struct perturb2_workspace * ppw2 = pppaw2->ppw2;
  double * pvecback = ppw2->pvecback;
  double * pvecthermo = ppw2->pvecthermo;
  double * pvecmetric = ppw2->pvecmetric;
  double * pvec_quadsources = ppw2->pvec_quadsources;
  
  /* Shortcuts for the wavemodes */
  double k1 = ppw2->k1;
  double k2 = ppw2->k2;
  double k = ppw2->k;
  double k_sq = ppw2->k_sq;
  double cosk1k2 = ppw2->cosk1k2;  
  double k1_sq = k1*k1;
  double k2_sq = k2*k2;
  double k1_dot_k2 = ppw2->k1_dot_k2;
  double * k1_m = ppw2->k1_m;
  double * k2_m = ppw2->k2_m;



  // ======================================================================================
  // =                                 Active effects                                     =
  // ======================================================================================

  /* By default we turn all optional effects off */
  int switch_sw=0;
  int switch_isw=0;
  
  if (ppt2->has_sw == _TRUE_) {
    switch_sw = 1;
  }
  
  if (ppt2->has_isw == _TRUE_) {

    switch_isw = 1;
  
    /* If the user asked for only the early ISW effect, then turn it off when after
    recombination */
    if ((ppt2->only_early_isw == _TRUE_) && (index_tau >= ppt2->index_tau_end_of_recombination))
      switch_isw = 0;    
  }


  // ======================================================================================
  // =                          Interpolate needed quantities                             =
  // ======================================================================================
  
  /* Call functions that will fill pvec___ arrays with useful quantities.  Do not alter the order
  in which these functions are called, since they all rely on the quantities computed by the
  previous ones. */

  /* Interpolate background-related quantities (pvecback) */
  class_call (background_at_tau(
                pba,
                tau,
                pba->normal_info, 
                pba->inter_closeby, 
                &(ppw2->last_index_back),
                ppw2->pvecback),
    pba->error_message,
    error_message);
  
  double a = pvecback[pba->index_bg_a];
  double a_sq = a*a;
  double Hc = pvecback[pba->index_bg_H]*a;
  double Hc_sq = Hc*Hc;
  double H = pvecback[pba->index_bg_H];
  double Y = log10(a/pba->a_eq);  
  
  /* Interpolate thermodynanics related quantities (pvecthermo) */
  class_call (thermodynamics_at_z(
                pba,
                pth,
                1./pvecback[pba->index_bg_a]-1.,  /* redshift z=1/a-1 */
                pth->inter_closeby,
                &(ppw2->last_index_thermo),
                ppw2->pvecback,
                ppw2->pvecthermo),
    pth->error_message,
    error_message);
  
  double r = pvecback[pba->index_bg_rho_g]/pvecback[pba->index_bg_rho_b];  
  double kappa_dot = pvecthermo[pth->index_th_dkappa];
  double exp_minus_kappa = pvecthermo[pth->index_th_exp_m_kappa];
  double g = pvecthermo[pth->index_th_g];
    
  /* Interpolate quadratic sources (ppw2->pvec_quadsources). Note that,
  because we need the quadratic part of the collision term, too, we 
  use perturb2_quadratic_sources() rather than perturb2_quadratic_sources_at_tau().
  In fact, the latter would only interpolate the full quadratic sources, while
  the former gives as an output that and the collisional part separately.
  We use interpolation because the quadratic sources were computed in
  ppt->tau_sampling while here we need them in ppt2->tau_sampling.
  This is achieved by specifying a negative index in the arguments of
  perturb2_quadratic_sources(). */
  if (ppt2->has_quadratic_sources == _TRUE_) {
    class_call (perturb2_quadratic_sources(
                  ppr,
                  ppr2,
                  pba,
                  pth,            
                  ppt,
                  ppt2,
                  -1, 
                  tau,
                  compute_total_and_collision,
                  ppw2->pvec_quadsources,
                  ppw2->pvec_quadcollision,
                  ppw2
                  ),
      ppt2->error_message,
      error_message);
  }
  

  if ((ppt2->k3_sampling == sym_k3_sampling) ) {
  
  	class_call (perturb_song_sources_at_tau_and_k (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                k1,
                tau,
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources1),
    	ppt->error_message,
    	ppt2->error_message);
    	
    	class_call (perturb_song_sources_at_tau_and_k (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                k2,
                tau,
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources2),
    	ppt->error_message,
    	ppt2->error_message);
  
  }
  else {
  /* Interpolate first-order quantities in tau and k1 (ppw2->psources_1) */
  	class_call (perturb_song_sources_at_tau (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                ppw2->index_k1,
                tau,
                ppt->qs_size[ppt->index_md_scalars],
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources1),
    	ppt->error_message,
    	error_message);
  
  /* Interpolate first-order quantities in tau and k2 (ppw2->psources_2) */
 	 	class_call (perturb_song_sources_at_tau (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                ppw2->index_k2,
                tau,
                ppt->qs_size[ppt->index_md_scalars],
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources2),
    	ppt->error_message,
    	error_message);
  }
  /* Define shorthands */
  double * pvec_sources1 = ppw2->pvec_sources1;
  double * pvec_sources2 = ppw2->pvec_sources2;

  
  /* Interpolate Einstein equations (pvecmetric) */
  class_call (perturb2_einstein (
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2,
                tau,
                y,
                ppw2),       /* We shall store the metric variables in ppw2->pvecmetric */
    ppt2->error_message,
    error_message);
  
  /* Shortcuts for metric variables */
  double phi=0, phi_1=0, phi_2=0, psi=0, psi_1=0, psi_2=0;
  double phi_prime_1=0, phi_prime_2=0, psi_prime_1=0, psi_prime_2=0;
  double phi_prime=0, psi_prime=0;
  double phi_exp=0, psi_exp=0, phi_exp_prime=0, psi_exp_prime=0;
  double omega_m1=0, omega_m1_prime=0, gamma_m2_prime=0;
  
  if (ppt->gauge == newtonian) {

    /* First order potentials (1->k1, 2->k2) */
    psi_1 = pvec_sources1[ppt->index_qs_psi];
    psi_2 = pvec_sources2[ppt->index_qs_psi];
  
    phi_1 = pvec_sources1[ppt->index_qs_phi];
    phi_2 = pvec_sources2[ppt->index_qs_phi];
  
    phi_prime_1 = pvec_sources1[ppt->index_qs_phi_prime];
    phi_prime_2 = pvec_sources2[ppt->index_qs_phi_prime];

    if (switch_isw == 1) {
      psi_prime_1 = pvec_sources1[ppt->index_qs_psi_prime];
      psi_prime_2 = pvec_sources2[ppt->index_qs_psi_prime];
    }


    /* Scalar potentials */
    if (ppr2->compute_m[0] == _TRUE_) {

      phi = y[ppw2->pv->index_pt2_phi];
      psi = pvecmetric[ppw2->index_mt2_psi];
      phi_prime = pvecmetric[ppw2->index_mt2_phi_prime];
            
      /* Compute psi_prime, needed to add the ISW effect */
      if (switch_isw == 1) {

        class_call (perturb2_compute_psi_prime (
                     ppr,
                     ppr2,
                     pba,
                     pth,
                     ppt,
                     ppt2,
                     tau,
                     y,
                     dy,
                     &(psi_prime),
                     ppw2),
          ppt2->error_message,
          error_message);
      }

      /* Exponential potentials, which appear in the metric as
        g_00 = -e^(2*psi_exp)
        g_ij = e^(-2*phi_exp) * delta_ij
      rather than
        g_00 = -(1+2*psi)
        g_ij = 1-2*phi ,
      so that, up to second order and expanding x~x^(1)+1/2x^(2),
        phi_exp = phi + 2*phi*phi
        psi_exp = psi - 2*psi*psi .
      (See eqs. 3.22, 4.97, 4.100 of my thesis for details.) Using one or the other representation
      does not affect the final result as long as we include all the sources in the line-of-sight
      integration. The result is affected, however, if we only include some of the sources,
      e.g. when including SW, ISW or quad_metric separately (cfr 4.97 and 4.100 of my thesis). */
      phi_exp = phi + 2*phi_1*phi_2;
      psi_exp = psi - 2*psi_1*psi_2;
      phi_exp_prime = phi_prime + 2*(phi_1*phi_prime_2 + phi_prime_1*phi_2);
      if (switch_isw == 1)
        psi_exp_prime = psi_prime - 2*(psi_1*psi_prime_2 + psi_prime_1*psi_2);
      
      /* Should we use the linear potentials or the exponential ones? */
      if (ppt2->use_exponential_potentials == _TRUE_) {
        phi = phi_exp;
        psi = psi_exp;
        phi_prime = phi_exp_prime;
        if (switch_isw == 1)
          psi_prime = psi_exp_prime;
      }
      
    } // end of m=0
    
    /* Vector potentials */
    if (ppr2->compute_m[1] == _TRUE_) {
      omega_m1 = y[ppw2->pv->index_pt2_omega_m1];
      omega_m1_prime = pvecmetric[ppw2->index_mt2_omega_m1_prime];
    }
    
    /* Tensor potentials */
    if (ppr2->compute_m[2] == _TRUE_)
      gamma_m2_prime = y[ppw2->pv->index_pt2_gamma_m2_prime];

  } // end of newtonian gauge
      

  
  // =========================================================================================
  // =                               Perturbed recombination                                 =
  // =========================================================================================
  
  /* Extract the density contrast of free electrons, delta_e. If perturbed recombination is
  not requested, we assume it to be equal to delta_b. */
  double delta_b_1 = pvec_sources1[ppt->index_qs_delta_b];
  double delta_b_2 = pvec_sources2[ppt->index_qs_delta_b];
  double delta_e_1 = delta_b_1;
  double delta_e_2 = delta_b_2;
  
  if (ppt2->has_perturbed_recombination_stz == _TRUE_) {
   
    /* If requested, we use the approximated formula in 3.23 of Senatore, Tassev, Zaldarriaga (STZ,
    http://arxiv.org/abs/0812.3652), which is valid for k < 1 in Newtonian gauge and it doesn't require
    to solve differential equations. */
    if (ppt2->perturbed_recombination_use_approx == _TRUE_) {
  
      double xe = pvecthermo[pth->index_th_xe];
      double xe_dot = pvecthermo[pth->index_th_dxe];
      delta_e_1 = delta_b_1 * (1. - 1/(3*Hc)*(xe_dot/xe));
      delta_e_2 = delta_b_2 * (1. - 1/(3*Hc)*(xe_dot/xe));
    }
    /* Full formalism, which involves computing the derivatives of the Q function (see STZ of appendix A
      of Pitrou et al. 2010) and solving an additional equation for delta_Xe in the first-order module. */
    else {
        
      delta_e_1 = delta_b_1 + pvec_sources1[ppt->index_qs_delta_Xe];
      delta_e_2 = delta_b_2 + pvec_sources2[ppt->index_qs_delta_Xe];
    }
  
    /* Some debug */
    // if ((ppw2->index_k1==0) && (ppw2->index_k2==0) && (ppw2->index_k3==0))
    //   fprintf (stderr, "%12.7g %12.7g %12.7g\n", ppt2->tau_sampling[index_tau], delta_e_1, delta_e_2);
  
  } // end of if(has_perturbed_recombination_stz)

     
  
  // =======================================================================================
  // =                                Build the LOS sources                                =
  // =======================================================================================
    
  /* We are now going to fill ppt2->sources, the multi-array containing the line-of-sight
  source functions for each type of source function and for each value of k1,k2,k3 and
  tau. This is the main product of the whole perturbations2.c module.
    
    The sources array is indexed as
  
      sources  [index_type]
               [index_k1]
               [index_k2]
               [index_tau*k3_size + index_k3]
  
  Due to symmetry properties, 'index_k2' runs from 0 to index_k1. Instead, 'index_k3'.
  runs from 0 to ppt2->k3_size[index_k1][index_k2] */
    
  /*  We split the sources for the line of sight (LOS) integration in three parts:
   *
   *   - the Q_L terms, which are quadratic and involve a metric perturbation and a photon perturbation,
   *   - the metric terms, that is any term that includes a metric perturbation (only up to l=2),
   *   - the scattering part, that is everything that is multiplied by the interaction rate 'kappa_dot'. 
   *
   *   The sum of the Q_L and metric terms yields the Liouville operator (minus the purely second-order
   * part of the free-streaming, which is on the left-hand-side of the LOS formula).  While the Q_L terms
   * are always quadratic in the perturbations, the metric terms can contain purely-second order metric variables
   * (which come from the redshift term of the Liouville operator). This is the case for the Sachs-Wolfe (SW)
   * effect and for the integrated SW effect.
   *
   *  As the metric variables do not usually vanish after recombination, if we include them in the LOS
   * integration, then we have to compute the LOS sources all the way to today.  The same applies
   * to the Q_L terms, with the added complication that they do not vanish for l>2.  If we want to include
   * the Q_L terms, we need to compute the sources up to today, for as many l's as we want to compute.
   * An exception is represented by the redshift term, which can be included exactly via the delta_tilde
   * transformation, as explained in arXiv:1302.0832.
   *
   *   The scattering part of the LOS sources is given by the multipole decomposition of the quadratic part of
   * the collision term.  For the monopole, which lacks a purely second-order collision term, there is an extra
   * kappa_dot*I(0,0) contribution.  The scattering part is the simplest part to treat, as it is strongly
   * suppressed after recombination.  When all other sources are turned off, the sources of the LOS integration
   * need to be computed only up to shortly after recombination.  Furthermore, the (l,m) contribution from
   * the quadratic sources is suppressed for l>2 multipoles by tight coupling.
   */



  // -------------------------------------------------------------------------------
  // -                               Test sources                                  -
  // -------------------------------------------------------------------------------  
  if (ppt2->use_test_source == _TRUE_) {

    sources(ppt2->index_tp2_T) = g * psi * psi;

		return _SUCCESS_;
	}

  
  // -------------------------------------------------------------------------------
  // -                               magnetic field                                  -
  // -------------------------------------------------------------------------------  
	if (ppt2->has_magnetic_field == _TRUE_) {

		for (int l=0; l<=1; ++l) {
			for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
				int m = ppt2->m[index_m];
  
        /* We shall increment the source term for this (l,m)-multipole with several contributions */
      	double source = 0;    
        
      	if (l==1) {
					source += mag(1,m);

				} // end of dipole sources
				
				sources(ppt2->index_tp2_M + lm(l,m)) = source;

        #pragma omp atomic
        ++ppt2->count_memorised_sources;
				
			}
  	}
  	    
	}
  
  // -------------------------------------------------------------------------------
  // -                            Photon temperature                               -
  // -------------------------------------------------------------------------------  
  
  if (ppt2->has_source_T == _TRUE_) {
    
    for (int l=0; l<=ppr2->l_max_los_t; ++l) {
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

        int m = ppt2->m[index_m];
  
        /* We shall increment the source term for this (l,m)-multipole with several contributions */
        double source = 0;        

        // *** Monopole source
        
        if (l==0) {

          /* Since the monopole doesn't have a purely second-order collision term, we have to include a
          kappa_dot*I_00 term to compensate the same term present in the line of sight formula. */
          if (ppt2->has_photon_monopole_in_los == _TRUE_)
            source += kappa_dot * I(0,0);

          /* Intrinsic metric term. This gives part of the ISW effect. The other half comes from the 
          integration by parts of the 4*k*psi term in the dipole. */
          if (ppt2->has_metric_in_los == _TRUE_) {
            source += 4 * phi_prime;
          }
          /* SW and ISW effects, coming from the monopole term 4*phi_prime and from the integration
          by parts of the 4*k*psi term in the dipole */
          else {
            source += switch_sw * (4 * kappa_dot * psi);
            source += switch_isw * (4 * (phi_prime + psi_prime));
          }

          /* Quadratic metric contribution from the Liouville operator. The monopole
          contribution exists only if using the standard linear potentials (cfr 4.97 and 4.100
          of http://arxiv.org/abs/1405.2280) */
          if ((ppt2->has_quad_metric_in_los == _TRUE_)
          && (ppt2->use_exponential_potentials == _FALSE_))
            source += 8 * (phi_1*phi_prime_2 + phi_2*phi_prime_1);

        } // end of monopole sources

        // *** Dipole source
  
        else if (l==1) {
  
          /* Second-order scattering term for the dipole */
          if (ppt2->has_pure_scattering_in_los == _TRUE_)
            source += four_thirds * kappa_dot * b(1,1,ppt2->m[index_m]);

          /* Intrinsic metric term. For m==0, when integrated by parts, this gives the SW effect and the
          other half of the ISW effect. We add it only if we integrate by parts. */
          if (ppt2->has_metric_in_los == _TRUE_) {
            if (m == 0) source += 4 * k * psi;
            if (m == 1) source += - 4 * omega_m1_prime;
          }
          else
            if (m == 1) source += switch_isw * (- 4 * omega_m1_prime);

          /* Quadratic metric contribution from the Liouville operator  */
          if (ppt2->has_quad_metric_in_los == _TRUE_) {
            /* Using the exponential potentials induces a sign difference in the dipole term
            (cfr 4.97 and 4.100 of my thesis) */
            if (ppt2->use_exponential_potentials == _FALSE_)
              source += - 4 * (k1_m[m+1]*psi_1*(psi_2-phi_2) + k2_m[m+1]*psi_2*(psi_1-phi_1));
            else
              source += - 4 * (k1_m[m+1]*psi_1*(-psi_2-phi_2) + k2_m[m+1]*psi_2*(-psi_1-phi_1));
          }

        } // end of dipole sources


        // *** Quadrupole source
  
        else if (l==2) {
  
          /* Second-order scattering term for the quadrupole */
          if (ppt2->has_pure_scattering_in_los == _TRUE_)
            source += kappa_dot * 0.1 * (ppw2->I_2m[m] - sqrt_6*ppw2->E_2m[m]);

          /* Tensor metric contribution */
          if (ppt2->has_metric_in_los == _TRUE_) {
            if (m == 2) source += 4 * gamma_m2_prime;
          else
            if (m == 2) source += switch_isw * (4 * gamma_m2_prime);
          }

        } // end of quadrupole sources

  
        // *** Contributions valid for all multipoles
  
        /* Scattering from quadratic sources of the form multipole times baryon_velocity */
        if (ppt2->has_quad_scattering_in_los == _TRUE_)
          source += dI_qc2(l,m);
        

        /* Time delay terms, i.e. terms arising from the free streaming part of the Liouville operator.
        These terms are suppressed by a factor 1/l. */
        if (ppt2->has_time_delay_in_los == _TRUE_)
          source -=   I_2_tilde(l+1) * c_plus_22(l,m)*k2*(phi_1+psi_1)
                    - I_2_tilde(l-1) * c_minus_22(l,m)*k2*(phi_1+psi_1)
                    /* Symmetrisation */
                    + I_1_tilde(l+1) * c_plus_11(l,m)*k1*(phi_2+psi_2)
                    - I_1_tilde(l-1) * c_minus_11(l,m)*k1*(phi_2+psi_2);

  
        /* Redshift term, that is the part of the Liouville operator which involves the
        derivative of the distribution function with respect to the photon momentum. */
        if (ppt2->has_redshift_in_los == _TRUE_)
          source -= - 4*phi_prime_1*I_2(l,m)
                    + I_2_tilde(l+1) * 4 * c_plus_12(l,m)*k1*psi_1
                    - I_2_tilde(l-1) * 4 * c_minus_12(l,m)*k1*psi_1
                    /* Symmetrisation */
                    - 4*phi_prime_2*I_1(l,m)
                    + I_1_tilde(l+1) * 4 * c_plus_21(l,m)*k2*psi_2
                    - I_1_tilde(l-1) * 4 * c_minus_21(l,m)*k2*psi_2;
        
      
        /* Lensing terms, i.e. terms arising from the lensing part of the Liouville operator */
        if (ppt2->has_lensing_in_los == _TRUE_)
          source -=   I_2_tilde(l+1) * r_plus_12(l,m)*k1*(phi_1+psi_1)
                    - I_2_tilde(l-1) * r_minus_12(l,m)*k1*(phi_1+psi_1)
                    /* Symmetrisation */
                    + I_1_tilde(l+1) * r_plus_21(l,m)*k2*(phi_2+psi_2)
                    - I_1_tilde(l-1) * r_minus_21(l,m)*k2*(phi_2+psi_2);
  
  
  
        // -------------------------------------------------------------------------------------------
        // -                             Sources for delta_tilde  (I)                                -
        // -------------------------------------------------------------------------------------------
  
        /* Compute the line of sight sources of the quantity 
        
          \tilde\Delta_{ab} = \Delta_{ab} - \Delta*\Delta/2
  
        in order to absorb the redshift term in the Boltzmann equation.  The \tilde\Delta
        variable was first introduced in Huang & Vernizzi (2013). Here we follow the general
        method in Fidler, Pettinari et al. (http://arxiv.org/abs/1401.3296) that includes
        polarisation.
  
        It is useful to use \tilde\Delta instead of the standard brightness
        delta because then the redshift quadratic term disappears. Note that in SONG we employ the
        convention whereby X ~ X^(1) + 1/2 * X^(2), so that our quadratic sources appear multiplied
        by a factor two with respect to those in Huang & Vernizzi (2013). */
        
        if ((ppt2->use_delta_tilde_in_los == _TRUE_) && (ppt2->has_quadratic_sources == _TRUE_)) {
        
          /* We shall compute the (l,m) multipole of delta*delta and delta*collision */
          double delta_delta_lm = 0;
          double delta_collision_lm = 0;
                  
          /* LOOP ON L1 */        
          for (int l1=0; l1 <= ppr2->l_max_los_quadratic_t; ++l1) {
            
            /* LOOP ON L2 - made in such a way that it always respects the triangular condition */
            for (int l2=abs(l1-l); l2 <= MIN(l1+l, ppr2->l_max_los_quadratic_t); ++l2) {

              /* The intensity field has even parity */
              if ((l1+l2+l)%2!=0)
                continue;
              
              /* LOOP ON M1 */
              for (int m1 = -l1; m1 <= l1; ++m1) {
          
                /* Enforce m1 + m2 - m = 0 */
                int m2 = m-m1;
          
                /* Coupling coefficient for this (l,l1,l2,m,m1,m2) */
                double coupling = ppt2->coupling_coefficients[ppt2->index_pf_t][lm(l,m)][l1][m1+ppt2->l1_max][l2];
          
                /* Collision term, as appearing on the righ-hand-side of delta_tilde_dot */
                double c_1 = rot_1(l2,m2)*pvec_sources1[ppt->index_qs_monopole_collision_g+l2];
                double c_2 = rot_2(l2,m2)*pvec_sources2[ppt->index_qs_monopole_collision_g+l2];
                                
                /* Increment delta*delta. There is no need to symmetrise with respect to k1<->k2 because,
                for intensity, the coupling coefficient is symmetric with respect to an exchange of l1<->l2
                (which for delta*delta, in turn, is equivalent to an echange in k1<->k2). The factor two comes
                from the fact that delta_ab*delta_cb + delta_cb*delta_ab = 2*delta_ab*delta_cb. */
                delta_delta_lm += coupling * 2 * I_1(l1,m1)*I_2(l2,m2);
                
                /* Increment delta*collision. The factor 0.5 comes from the symmetrisation with respect
                to k1<->k2, while the factor 2 from the fact that the coupling coefficients are symmetric
                with respect to l1<->l2 (which, for the intensity, is equivalent to exchange 'delta' and
                'collision'). This can be seen from the intensity formula in the
                section 'Compute the general coefficients' of the function 'perturb2_get_lm_lists'. */
                delta_collision_lm += coupling * 2
                  * 0.5 *(I_1(l1,m1)*c_2 + I_2(l1,m1)*c_1); /* k1 <-> k2 */
        
                /* Note that, in principle, the delta*collision term should be symmetrised with respect
                to the exchange of 'delta_ab' and 'C_ab' (see Eq. 3.10 of http://arxiv.org/abs/1401.3296,
                or the intensity equations in the section 'Compute the general coefficients' of the
                function 'perturb2_get_lm_lists'). For the intensity sources, we can omit such symmetrisation
                because of the symmetry of the coupling coefficients with respect to l1<->l2. You can prove
                that the result does not change by commenting the above line and uncommenting the following lines. */
                
                // double c_k1_l1 = rot_1(l1,m1)*pvec_sources1[ppt->index_qs_monopole_collision_g+l1];
                // double c_k1_l2 = rot_1(l2,m2)*pvec_sources1[ppt->index_qs_monopole_collision_g+l2];
                // double c_k2_l1 = rot_2(l1,m1)*pvec_sources2[ppt->index_qs_monopole_collision_g+l1];
                // double c_k2_l2 = rot_2(l2,m2)*pvec_sources2[ppt->index_qs_monopole_collision_g+l2];
                //                 
                // delta_collision_lm += coupling *  0.5 *
                //   (I_1(l1,m1)*c_k2_l2 + I_2(l1,m1)*c_k1_l2
                //   +c_k1_l1*I_2(l2,m2) + c_k2_l1*I_1(l2,m2));
        
              } // end of for (m1)            
            } // end of for (l2)
          } // end of for (l1)
          
          /* Extra sources for delta tilde. The factor 'quad_coefficient' accounts for the factorial in our
          perturbative expansion. The factor 0.5 in delta_delta_lm accounts for the 1/2 factor in the
          definition of delta_tilde. */
          source += - quad_coefficient * kappa_dot * 0.5*delta_delta_lm
                    - quad_coefficient * kappa_dot * delta_collision_lm;
          
        } // end of sources for delta_tilde (intensity)

  
        // ----------------------------------------------------------------
        // -                     Sum up the sources                       -
        // ----------------------------------------------------------------
        
        /* Uncomment to include in the LOS sources all the terms that were included in the differential system */
        // source = dI_qs2(l,m);
  
        /* All the sources in the line of sight integration are multiplied by exp(-kappa(tau,tau0)),
        which is extremely small before recombination. */
        source *= exp_minus_kappa;
  
        /* Fill the ppt2->sources array, and assign the labels */
        sources(ppt2->index_tp2_T + lm(l,m)) = source;

        #pragma omp atomic
        ++ppt2->count_memorised_sources;
  
        /* Debug - print the computed source */
        // printf ("(l=%d,m=%d): source=%24.17g\n", l, m, source);
  
      }  // end for (m)
    } // end for (l)
  } // end of temperature sources

  
  
  // -------------------------------------------------------------------------------
  // -                            E-mode polarisation                              -
  // -------------------------------------------------------------------------------  

  if (ppt2->has_source_E == _TRUE_) {
    for (int l=0; l<=ppr2->l_max_los_p; ++l) {
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

        int m = ppt2->m[index_m];

        /* E-modes start from l=2 */
        if (l<2) {
          #pragma omp atomic
          ++ppt2->count_memorised_sources;
          continue;
        }

        /* We shall increment the source term for this (l,m)-multipole with several contributions */
        double source = 0;

        // *** Quadrupole source

        if (l==2) {
          /* Second-order scattering term for the quadrupole */
          if (ppt2->has_pure_scattering_in_los == _TRUE_)
            source += - sqrt_6 * kappa_dot * 0.1 * (ppw2->I_2m[m] - sqrt_6*ppw2->E_2m[m]);
        }


        // *** Contributions valid for all multipoles
          
        /* Scattering from quadratic sources of the form multipole times baryon_velocity */
        if (ppt2->has_quad_scattering_in_los == _TRUE_)
          source += dE_qc2(l,m);


        /* Time delay terms, i.e. terms arising from the free streaming part of the Liouville operator */
        if (ppt2->has_time_delay_in_los == _TRUE_)
          source -=   E_2_tilde(l+1) * d_plus_22(l,m)*k2*(phi_1+psi_1)
                    - E_2_tilde(l-1) * d_minus_22(l,m)*k2*(phi_1+psi_1)
                    /* Symmetrisation */
                    + E_1_tilde(l+1) * d_plus_11(l,m)*k1*(phi_2+psi_2)
                    - E_1_tilde(l-1) * d_minus_11(l,m)*k1*(phi_2+psi_2);
        
          
        /* Redshift term, that is the part of the Liouville operator which involves the derivative of the
        distribution function with respect to the photon momentum. */
        if (ppt2->has_redshift_in_los == _TRUE_)
          source -= - 4*phi_prime_1*E_2(l,m)
                    + E_2_tilde(l+1) * 4 * d_plus_12(l,m)*k1*psi_1
                    - E_2_tilde(l-1) * 4 * d_minus_12(l,m)*k1*psi_1
                    /* Symmetrisation */
                    - 4*phi_prime_2*E_1(l,m)
                    + E_1_tilde(l+1) * 4 * d_plus_21(l,m)*k2*psi_2
                    - E_1_tilde(l-1) * 4 * d_minus_21(l,m)*k2*psi_2;
        
              
        /* Lensing terms, i.e. terms arising from the lensing part of the Liouville operator */
        if (ppt2->has_lensing_in_los == _TRUE_)
          source -=   E_2_tilde(l+1) * k_plus_12(l,m)*k1*(phi_1+psi_1)
                    - E_2_tilde(l-1) * k_minus_12(l,m)*k1*(phi_1+psi_1)
                    /* Symmetrisation */
                    + E_1_tilde(l+1) * k_plus_21(l,m)*k2*(phi_2+psi_2)
                    - E_1_tilde(l-1) * k_minus_21(l,m)*k2*(phi_2+psi_2);
  

        // -------------------------------------------------------------------------------------------
        // -                             Sources for delta_tilde  (E)                                -
        // -------------------------------------------------------------------------------------------
          
        if ((ppt2->use_delta_tilde_in_los == _TRUE_) && (ppt2->has_quadratic_sources == _TRUE_)) {
        
          /* We shall compute the (l,m) multipole of delta*delta and delta*collision */
          double delta_delta_lm = 0;
          double delta_collision_lm = 0;
                  
          /* LOOP ON L1 */        
          for (int l1=0; l1 <= ppr2->l_max_los_quadratic_p; ++l1) {
            
            /* LOOP ON L2 - made in such a way that it always respects the triangular condition */
            for (int l2=abs(l1-l); l2 <= MIN(l1+l, ppr2->l_max_los_quadratic_p); ++l2) {
              
              /* The E-mode polarisation has even parity */
              if ((l1+l2+l)%2!=0)
                continue;
              
              /* LOOP ON M1 */
              for (int m1 = -l1; m1 <= l1; ++m1) {
          
                /* Enforce m1 + m2 - m = 0 */
                int m2 = m-m1;
          
                /* Coupling coefficient for this (l,l1,l2,m,m1,m2) */
                double coupling = ppt2->coupling_coefficients[ppt2->index_pf_e][lm(l,m)][l1][m1+ppt2->l1_max][l2];
          
                /* Collision term, as appearing on the righ-hand-side of delta_tilde_dot.  */
                double c_I_k1_l1 = rot_1(l1,m1)*pvec_sources1[ppt->index_qs_monopole_collision_g+l1];
                double c_I_k2_l1 = rot_2(l1,m1)*pvec_sources2[ppt->index_qs_monopole_collision_g+l1];
                double c_E_k1_l2 = rot_1(l2,m2)*pvec_sources1[ppt->index_qs_monopole_collision_E+l2];
                double c_E_k2_l2 = rot_2(l2,m2)*pvec_sources2[ppt->index_qs_monopole_collision_E+l2];
                
                /* Increment delta*delta. The factor 0.5 comes from the symmetrisation with respect to k1<->k2.
                The factor 2 comes from the fact that delta_ab*delta_cb + delta_cb*delta_ab = 2*delta_ab*delta_cb.*/
                delta_delta_lm += coupling *  2 * 0.5 * (I_1(l1,m1)*E_2(l2,m2) + I_2(l1,m1)*E_1(l2,m2));
        
                /* Increment delta*collision. The delta*collision contribution has twice the terms of delta*delta
                because it is symmetric with respect to the exchange of delta<->collision. The 0.5 factor comes
                from the syymetrisation with respect to k1<->k2 */
                delta_collision_lm += coupling *  0.5 *
                  (I_1(l1,m1)*c_E_k2_l2 + c_I_k1_l1*E_2(l2,m2)   /* delta<->collision */
                  +I_2(l1,m1)*c_E_k1_l2 + c_I_k2_l1*E_1(l2,m2)); /* k1<->k2 */
                  
              } // end of for (m1)            
            } // end of for (l2)
          } // end of for (l1)
          
          /* Extra sources for delta tilde. The factor 'quad_coefficient' accounts for the factorial in our
          perturbative expansion. The factor 0.5 in delta_delta_lm accounts for the 1/2 factor in the
          definition of delta_tilde. */
          source += - quad_coefficient * kappa_dot * 0.5*delta_delta_lm
                    - quad_coefficient * kappa_dot * delta_collision_lm;
          
        } // end of sources for delta_tilde (E-modes)

  
        // ----------------------------------------------------------------
        // -                     Sum up the sources                       -
        // ----------------------------------------------------------------
        
        /* Uncomment to include in the LOS sources only the terms that were included in the differential system */
        // source = dE_qs2(l,m) + kappa_dot*dE_qs2(l,m);
  
        /* All the sources in the line of sight integration are multiplied by exp(-kappa(tau,tau0)),
        which is extremely small before recombination. */
        source *= exp_minus_kappa;
  
        /* Fill the ppt2->sources array, and assign the labels */
        sources(ppt2->index_tp2_E + lm(l,m)) = source;

        #pragma omp atomic
        ++ppt2->count_memorised_sources;

      }  // end for (m)
    } // end for (l)
  } // end of E-mode sources

  
  
  // -------------------------------------------------------------------------------
  // -                            B-mode polarisation                              -
  // -------------------------------------------------------------------------------  
  
  if (ppt2->has_source_B == _TRUE_) {
    for (int l=0; l<=ppr2->l_max_los_p; ++l) {
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {

        int m = ppt2->m[index_m];

        /* B-modes start from l=2 and they don't have a scalar part */
        if ((l<2) || (m==0)) {
          #pragma omp atomic
          ++ppt2->count_memorised_sources;
          continue;
        }

        /* We shall increment the source term for this (l,m)-multipole with several contributions */
        double source = 0;

        /* Since we have assumed that the B-modes are negligible at first-order, their quadratic
        sources involve only E^(1). Note that they mix with the E-modes only through the Liouville
        operator, not through the collision term, which is the way I and E mix. */

        /* Scattering from quadratic sources of the form multipole times baryon_velocity */
        if (ppt2->has_quad_scattering_in_los == _TRUE_)
          source += dB_qc2(l,m);


        /* Time delay terms, i.e. terms arising from the free streaming part of the Liouville operator */
        if (ppt2->has_time_delay_in_los == _TRUE_)
          source -= - k2*E_2_tilde(l) * d_zero_22(l,m)*(phi_1+psi_1)
                    /* Symmetrisation */
                    - k1*E_1_tilde(l) * d_zero_11(l,m)*(phi_2+psi_2);
        
          
        /* Redshift term, that is the part of the Liouville operator which involves the derivative of the
        distribution function with respect to the photon momentum. */
        if (ppt2->has_redshift_in_los == _TRUE_)
          source -= - E_2_tilde(l) * 4 * d_zero_12(l,m)*k1*psi_1
                    /* Symmetrisation */
                    - E_1_tilde(l) * 4 * d_zero_21(l,m)*k2*psi_2;


        /* Lensing terms, i.e. terms arising from the lensing part of the Liouville operator */
        if (ppt2->has_lensing_in_los == _TRUE_)
          source -= - E_2_tilde(l) * k_zero_12(l,m)*k1*(phi_1+psi_1)
                    /* Symmetrisation */
                    - E_1_tilde(l) * k_zero_21(l,m)*k2*(phi_2+psi_2);


        // -------------------------------------------------------------------------------------------
        // -                             Sources for delta_tilde  (B)                                -
        // -------------------------------------------------------------------------------------------
          
        if ((ppt2->use_delta_tilde_in_los == _TRUE_) && (ppt2->has_quadratic_sources == _TRUE_)) {
        
          /* We shall compute the (l,m) multipole of delta*delta and delta*collision */
          double delta_delta_lm = 0;
          double delta_collision_lm = 0;
                  
          /* LOOP ON L1 */        
          for (int l1=0; l1 <= ppr2->l_max_los_quadratic_p; ++l1) {
            
            /* LOOP ON L2 - made in such a way that it always respects the triangular condition */
            for (int l2=abs(l1-l); l2 <= MIN(l1+l, ppr2->l_max_los_quadratic_p); ++l2) {

              /* The B-mode polarisation has odd parity */
              if ((l1+l2+l)%2==0)
                continue;
              
              /* LOOP ON M1 */
              for (int m1 = -l1; m1 <= l1; ++m1) {
          
                /* Enforce m1 + m2 - m = 0 */
                int m2 = m-m1;
          
                /* Coupling coefficient for this (l,l1,l2,m,m1,m2) */
                double coupling = ppt2->coupling_coefficients[ppt2->index_pf_b][lm(l,m)][l1][m1+ppt2->l1_max][l2];
          
                /* Collision term, as appearing on the righ-hand-side of delta_tilde_dot.  */
                double c_I_k1_l1 = rot_1(l1,m1)*pvec_sources1[ppt->index_qs_monopole_collision_g+l1];
                double c_I_k2_l1 = rot_2(l1,m1)*pvec_sources2[ppt->index_qs_monopole_collision_g+l1];
                double c_E_k1_l2 = rot_1(l2,m2)*pvec_sources1[ppt->index_qs_monopole_collision_E+l2];
                double c_E_k2_l2 = rot_2(l2,m2)*pvec_sources2[ppt->index_qs_monopole_collision_E+l2];
                
                /* Increment delta*delta. The factor 0.5 comes from the symmetrisation with respect to k1<->k2.
                The factor 2 comes from the fact that delta_ab*delta_cb + delta_cb*delta_ab = 2*delta_ab*delta_cb.*/
                delta_delta_lm += coupling *  2 * 0.5 * (I_1(l1,m1)*E_2(l2,m2) + I_2(l1,m1)*E_1(l2,m2));
        
                /* Increment delta*collision. The delta*collision contribution has twice the terms of delta*delta
                because it is symmetric with respect to the exchange of delta<->collision. The 0.5 factor comes
                from the syymetrisation with respect to k1<->k2 */
                delta_collision_lm += coupling *  0.5 *
                  (I_1(l1,m1)*c_E_k2_l2 + c_I_k1_l1*E_2(l2,m2)   /* delta<->collision */
                  +I_2(l1,m1)*c_E_k1_l2 + c_I_k2_l1*E_1(l2,m2)); /* k1<->k2 */
                  
              } // end of for (m1)            
            } // end of for (l2)
          } // end of for (l1)
          
          /* Extra sources for delta tilde. The factor 'quad_coefficient' accounts for the factorial in our
          perturbative expansion. The factor 0.5 in delta_delta_lm accounts for the 1/2 factor in the
          definition of delta_tilde. */
          source += - quad_coefficient * kappa_dot * 0.5*delta_delta_lm
                    - quad_coefficient * kappa_dot * delta_collision_lm;
          
        } // end of sources for delta_tilde (B-modes)

  
        // ----------------------------------------------------------------
        // -                     Sum up the sources                       -
        // ----------------------------------------------------------------
        
        /* Uncomment to include in the LOS sources only the terms that were included in the differential system */
        // source = dB_qs2(l,m) + kappa_dot*dB_qs2(l,m);
  
        /* All the sources in the line of sight integration are multiplied by exp(-kappa(tau,tau0)),
        which is extremely small before recombination. */
        source *= exp_minus_kappa;
  
        /* Fill the ppt2->sources array, and assign the labels */
        sources(ppt2->index_tp2_B + lm(l,m)) = source;

        #pragma omp atomic
        ++ppt2->count_memorised_sources;

      }  // end for (m)
    } // end for (l)
  } // end of B-mode sources
  
  
  
  
  // -------------------------------------------------------------------------------------------
  // -                             Sources for \tilde\Delta                                     -
  // -------------------------------------------------------------------------------------------
  
  /* Compute the line of sight sources of the quantity 
        
    \tilde\Delta_{ab} = \Delta_{ab} - \Delta*\Delta/2
  
  in order to absorb the redshift term in the Boltzmann equation.  The \tilde\Delta
  variable was first introduced in Huang & Vernizzi (2013). Here we follow the general
  method in Fidler, Pettinari et al. (http://arxiv.org/abs/1401.3296) that includes
  polarisation.
  
  It is useful to use \tilde\Delta instead of the standard brightness
  delta because then the redshift quadratic term disappears. Note that in SONG we employ the
  convention whereby X ~ X^(1) + 1/2 * X^(2), so that our quadratic sources appear multiplied
  by a factor two with respect to those in Huang & Vernizzi (2013). */
  
  // if ((ppt2->use_delta_tilde_in_los == _TRUE_) && (ppt2->has_quadratic_sources == _TRUE_)) {
  //   
  //   /* We shall loop on the various CMB fields (I,E,B) */
  //   int l_max_quad;
  //   double *X_A, *X_B, *Y_A, *Y_B;
  //   
  //   for (int index_pf=0; index_pf < ppt2->pf_size; ++index_pf) {
  // 
  //     if ((ppt2->has_source_T == _TRUE_) && (index_pf == ppt2->index_pf_t)) {
  //       
  //       X_A = pvec_sources1[ppt->index_qs_monopole_g];
  //       Y = pvec_sources1[ppt->index_qs_monopole_g];
  //       
  //       monopole_index = ppt->index_qs_monopole_g;
  //       monopole_collision_index = ppt->index_qs_monopole_collision_g;
  //       l_max_quad = ppr2->l_max_los_quadratic_t;
  //     }
  //     else if ((ppt2->has_source_E == _TRUE_) && (index_pf == ppt2->index_pf_e)) {
  //       monopole_index = ppt->index_qs_monopole_E;
  //       monopole_collision_index = ppt->index_qs_monopole_collision_E;
  //       l_max_quad = ppr2->l_max_los_quadratic_p;
  //     }
  //     else if ((ppt2->has_source_B == _TRUE_) && (index_pf == ppt2->index_pf_b)) {
  //       monopole_index = ppt->index_qs_monopole_g;
  //       monopole_collision_index = ppt->index_qs_monopole_collision_g;
  //       l_max_quad = ppr2->l_max_los_quadratic_p;
  //     }
  // 
  //     /* We shall compute the (l,m) multipole of delta*delta and delta*collision */
  //     double delta_delta_lm = 0;
  //     double delta_collision_lm = 0;
  //           
  //     /* LOOP ON L1 */        
  //     for (int l1=0; l1 <= l_max_quad; ++l1) {
  //     
  //       /* LOOP ON L2 - made in such a way that it always respects the triangular condition */
  //       for (int l2=abs(l1-l); l2 <= MIN(l1+l, l_max_quad); ++l2) {
  //       
  //         if ((l1+l2+l)%2!=0)
  //           continue;
  //       
  //         /* LOOP ON M1 */
  //         for (int m1 = -l1; m1 <= l1; ++m1) {
  //   
  //           /* Enforce m1 + m2 - m = 0 */
  //           int m2 = m-m1;
  //   
  //           /* Coupling coefficient for this (l,l1,l2,m,m1,m2) */
  //           double coupling = ppt2->coupling_coefficients[index_pf][lm(l,m)][l1][m1+ppt2->l1_max][l2];
  //   
  //           /* Collision term, as appearing on the righ-hand-side of delta_dot */
  //           double c_1 = rot_1(l2,m2)*pvec_sources1[ppt->index_qs_monopole_collision_g+l2];
  //           double c_2 = rot_2(l2,m2)*pvec_sources2[ppt->index_qs_monopole_collision_g+l2];
  //                         
  //           /* Increment the sums. The factor 0.5 comes from the symmetrisation with 
  //           respect to k1<->k2. */
  //           delta_delta_lm += coupling *  0.5 * (I_1(l1,m1)*I_2(l2,m2) + I_2(l1,m1)*I_1(l2,m2));
  //           delta_collision_lm += coupling *  0.5 * (I_1(l1,m1)*c_2 + I_2(l1,m1)*c_1);
  //           
  //         } // end of for (m1)            
  //       } // end of for (l2)
  //     } // end of for (l1)
  //   
  //     /* Extra sources for delta tilde. The factor 'quad_coefficient' accounts for the factorial in our
  //     perturbative expansion. The factor 0.5 in delta_delta_lm accounts for the 1/2 factor in the
  //     definition of delta_tilde. */
  //     source += - quad_coefficient * kappa_dot * 0.5*delta_delta_lm
  //               - quad_coefficient * kappa_dot * delta_collision_lm;
  // 
  //   } // end of for (I,E,B...)
  // 
  // } // end of if has_delta_tilde
  
  return _SUCCESS_;

} // end of perturb2_sources







int perturb2_print_variables(double tau,
          double * y,
          double * dy,
          int index_tau,          
          void * parameters_and_workspace,
          ErrorMsg error_message
          )
{
  
  
  /* Define shortcuts to avoid heavy notations */
  struct perturb2_parameters_and_workspace * pppaw2 = parameters_and_workspace;
  struct precision * ppr = pppaw2->ppr;
  struct precision2 * ppr2 = pppaw2->ppr2;
  struct background * pba = pppaw2->pba;
  struct thermo * pth = pppaw2->pth;
  struct perturbs * ppt = pppaw2->ppt;
  struct perturbs2 * ppt2 = pppaw2->ppt2;  
  struct perturb2_workspace * ppw2 = pppaw2->ppw2;
  double k1 = ppt2->k[ppw2->index_k1];  
  double k2 = ppt2->k[ppw2->index_k2];
  
  if ((ppt2->k3_sampling == sym_k3_sampling) ) {
  
  	k1 = ppw2->k1;
  	k2 = ppw2->k2;
  }
  
  
  double * pvecback = ppw2->pvecback;
  double * pvecthermo = ppw2->pvecthermo;
  double * pvecmetric = ppw2->pvecmetric;


  if (ppw2->index_k1==0) {

      fprintf(stdout,"%e   %e   %e   %e  ",
        k1,
        k2,
        tau,
        pvecmetric[ppw2->index_mt2_psi]
      );

      fprintf(stdout,"\n");
  }
  
  return _SUCCESS_;

}





int what_if_ndf15_fails(int (*derivs)(double x, 
          double * y, 
          double * dy, 
          void * parameters_and_workspace,
          ErrorMsg error_message),
        double x_ini,
        double x_end,
        double * y, 
        int * used_in_output,
        int y_size,
        void * parameters_and_workspace_for_derivs,
        double tolerance, 
        double minimum_variation,
        int (*evaluate_timescale)(double x, 
          void * parameters_and_workspace,
          double * timescale,
          ErrorMsg error_message),
        double timestep_over_timescale,
        double * x_sampling,
        int x_size,
        int (*output)(double x,
          double y[],
          double dy[],
          int index_x,
          void * parameters_and_workspace,
          ErrorMsg error_message),
        int (*print_variables)(double x,
             double y[], 
             double dy[],
             void * parameters_and_workspace,
             ErrorMsg error_message),
        ErrorMsg error_message)
{
          
  
  /* Alleviate notation */
  struct perturb2_parameters_and_workspace * pppaw2 = parameters_and_workspace_for_derivs;
  struct perturbs2 * ppt2 = pppaw2->ppt2;
  struct perturb2_workspace * ppw2 = pppaw2->ppw2;
  
  
  /* Print info */        
  if (ppt2->perturbations2_verbose > 0) {
    printf("     * error in ndf15: '%s'\n", error_message);
    printf("     * recomputing mode (k1,k2,k,cosk1k2) = (%.17f,%.17f,%.17f,%.17f) = (%d,%d,%d) using Runge-Kutta method\n",
      ppw2->k1, ppw2->k2, ppw2->k, ppw2->cosk1k2, ppw2->index_k1, ppw2->index_k2, ppw2->index_k3);
    fflush(stdout);
  }
    
    
  /* We pass to the Runge-Kutta evolver the same arguments that were passed to the ndf15 one.  Note however
  that rk relies on the function timescale_and_approximation while ndf15 does not, so make sure that it
  points to a valid function. In principle we could also pass an exit_strategy function to evolver_rk
  (e.g. what_if_rk_fails). */
  extern int evolver_rk();
  class_call (evolver_rk(
                derivs,
                x_ini,
                x_end,
                y, 
                used_in_output,
                y_size, 
                parameters_and_workspace_for_derivs,
                tolerance, 
                minimum_variation, 
                evaluate_timescale,
                timestep_over_timescale, 
                x_sampling, 
                x_size,
                output,
                print_variables,
                NULL,
                error_message),
    error_message,
    error_message);


          
  return _SUCCESS_;
          
};












/**
 * Output to file the state of the differential system.
 *
 * This is the point where you can output to file or to screen the second-order transfer
 * functions, or any other quantity you are interested into.
 * 
 * The transfer functions will be saved to the file ppt2->transfers_file, whose name
 * is specified in the parameter file via the parameter transfers_filename. The
 * qudratic sources will be saved to the file ppt2->quadsources_file, whose path is
 * specified via the quadsources_filename parameter.
 *
 * Note that this function is called from within the evolver at each time step, so
 * expect the outputted files to be large.
 */
int perturb2_save_early_transfers (
          double tau,
          double * y,
          double * dy,
          void * parameters_and_workspace,
          ErrorMsg error_message
          )
{
  
  /* Shortcuts for the structures */
  struct perturb2_parameters_and_workspace * pppaw2 = parameters_and_workspace;
  struct precision * ppr = pppaw2->ppr;
  struct precision2 * ppr2 = pppaw2->ppr2;
  struct background * pba = pppaw2->pba;
  struct thermo * pth = pppaw2->pth;
  struct perturbs * ppt = pppaw2->ppt;
  struct perturbs2 * ppt2 = pppaw2->ppt2;  
  struct perturb2_workspace * ppw2 = pppaw2->ppw2;
  double * pvecback = ppw2->pvecback;
  double * pvecthermo = ppw2->pvecthermo;
  double * pvecmetric = ppw2->pvecmetric;
  double * pvec_quadsources = ppw2->pvec_quadsources;
  
  /* Shortcuts for the wavemodes */
  double k1 = ppw2->k1;
  double k2 = ppw2->k2;
  double k = ppw2->k;
  double k_sq = ppw2->k_sq;
  double cosk1k2 = ppw2->cosk1k2;  
  double k1_sq = k1*k1;
  double k2_sq = k2*k2;
  double k1_dot_k2 = ppw2->k1_dot_k2;
  double * k1_m = ppw2->k1_m;
  double * k2_m = ppw2->k2_m;
  double * k1_ten_k2 = ppw2->k1_ten_k2;

  /* Update the number of steps in the differential system */
  ppw2->n_steps++;

  /* Debug - Investigate how many steps are needed to evolve the current mode.
  If it exceeds 5000 steps, you should probably be suspicious. */
  // printf ("ppw2->n_steps = %d\n", ppw2->n_steps);

  // ======================================================================================
  // =                          Interpolate needed quantities                             =
  // ======================================================================================
  
  /* Call functions that will fill pvec___ arrays with useful quantities.  Do not alter
  the order in which these functions are called, since they all rely on the quantities
  computed by the previous ones. */
         
  /* Interpolate background-related quantities (pvecback) */
  class_call (background_at_tau(
                pba,
                tau,
                pba->long_info, 
                pba->inter_closeby, 
                &(ppw2->last_index_back),
                ppw2->pvecback),
    pba->error_message,
    error_message);
  
  double a = pvecback[pba->index_bg_a];
  double a_sq = a*a;
  double H = pvecback[pba->index_bg_H];
  double Hc = pvecback[pba->index_bg_H]*a;
  double Hc_sq = Hc*Hc;
  double Y = log10(a/pba->a_eq);  
  double Omega_m = ppw2->pvecback[pba->index_bg_Omega_m];
  double Omega_r = ppw2->pvecback[pba->index_bg_Omega_r];
  double Omega_m0 = pba->Omega0_cdm + pba->Omega0_b;
  double Omega_r0 = pba->Omega0_g + pba->Omega0_ur;
  
  /* Interpolate thermodynaics-related quantities (pvecthermo) */
  class_call (thermodynamics_at_z(
                pba,
                pth,
                1./pvecback[pba->index_bg_a]-1.,  /* redshift z=1/a-1 */
                pth->inter_closeby,
                &(ppw2->last_index_thermo),
                ppw2->pvecback,
                ppw2->pvecthermo),
    pth->error_message,
    error_message);
  
  double kappa_dot = pvecthermo[pth->index_th_dkappa];
  double r = pvecback[pba->index_bg_rho_g]/pvecback[pba->index_bg_rho_b];
  double tau_c = 1/kappa_dot;

  /* Interpolate quadratic sources by filling ppw2->pvec_quadsources
  and ppw2->pvec_quadcollision */
  if (ppt2->has_quadratic_sources == _TRUE_) {
    class_call (perturb2_quadratic_sources(
                  ppr,
                  ppr2,
                  pba,
                  pth,
                  ppt,
                  ppt2,
                  -1,
                  tau,
                  compute_total_and_collision,
                  ppw2->pvec_quadsources,
                  ppw2->pvec_quadcollision,
                  ppw2
                  ),
      ppt2->error_message,
      error_message);
  }

  
  /* Interpolate first-order quantities (ppw2->psources_1) */
  if ((ppt2->k3_sampling == sym_k3_sampling) ) {
  
  	class_call (perturb_song_sources_at_tau_and_k (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                k1,
                tau,
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources1),
    	ppt->error_message,
    	ppt2->error_message);
    	
    	class_call (perturb_song_sources_at_tau_and_k (
                ppr,
                ppt,
                ppt->index_md_scalars,
                ppt2->index_ic_first_order,
                k2,
                tau,
                ppt->inter_normal,
                &(ppw2->last_index_sources),
                ppw2->pvec_sources2),
    	ppt->error_message,
    	ppt2->error_message);
  
  }
  else {
  	class_call (perturb_song_sources_at_tau (
               ppr,
               ppt,
               ppt->index_md_scalars,
               ppt2->index_ic_first_order,
               ppw2->index_k1,
               tau,
               ppt->qs_size[ppt->index_md_scalars],
               ppt->inter_normal,
               &(ppw2->last_index_sources),
               ppw2->pvec_sources1),
     	ppt->error_message,
     	ppt2->error_message);
  
  /* Interpolate first-order quantities (ppw2->psources_2) */
  	class_call (perturb_song_sources_at_tau (
               ppr,
               ppt,
               ppt->index_md_scalars,
               ppt2->index_ic_first_order,
               ppw2->index_k2,
               tau,
               ppt->qs_size[ppt->index_md_scalars],
               ppt->inter_normal,
               &(ppw2->last_index_sources),                 
               ppw2->pvec_sources2),
    	ppt->error_message,
    	ppt2->error_message);
    	
  }

  
  /* Define shorthands */
  double * pvec_sources1 = ppw2->pvec_sources1;
  double * pvec_sources2 = ppw2->pvec_sources2;

  /* Interpolate Einstein equations (pvecmetric) */
  class_call (perturb2_einstein (
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2,
                tau,
                y,
                ppw2),
    ppt2->error_message,
    error_message);

  

  
  // ====================================================================================
  // =                                Newtonian Gauge                                   =
  // ====================================================================================

  double phi=0, psi=0, psi_prime=0;
  double psi_1=0, psi_2=0, phi_1=0, phi_2=0, phi_prime_1=0, phi_prime_2=0;
  double omega_m1=0, omega_m1_constraint=0;
  double gamma_m2=0, gamma_m2_prime=0;

  
  if (ppt->gauge == newtonian) {

    /* - First-order quantities */

    psi_1 = pvec_sources1[ppt->index_qs_psi];
    psi_2 = pvec_sources2[ppt->index_qs_psi];
    phi_1 = pvec_sources1[ppt->index_qs_phi];
    phi_2 = pvec_sources2[ppt->index_qs_phi];
    phi_prime_1 = pvec_sources1[ppt->index_qs_phi_prime];
    phi_prime_2 = pvec_sources2[ppt->index_qs_phi_prime];
    
    /* - Second-order scalar quantities */

    /* Scalar potentials phi and psi */
    if (ppr2->compute_m[0] == _TRUE_) {
      psi = pvecmetric[ppw2->index_mt2_psi];
      phi = y[ppw2->pv->index_pt2_phi];
    }
    
    /* Conformal time derivative of psi */
    if ((ppt2->has_isw == _TRUE_) && (ppr2->compute_m[0])) {

      class_call(perturb2_compute_psi_prime (
                   ppr,
                   ppr2,
                   pba,
                   pth,
                   ppt,
                   ppt2,
                   tau,
                   y,
                   dy,
                   &(psi_prime),
                   ppw2),
        ppt2->error_message,
        error_message);
    }


    /* - Vector potential */
    
    /* Compute the value of omega_m1 (the vector potential) using the constraint equation.
    This is eq. 3.98 of http://arxiv.org/abs/1405.2280, obtained from the the G^i_0 part
    of Einstein equation.  */
    if (ppr2->compute_m[1] == _TRUE_) {

      omega_m1 = y[ppw2->pv->index_pt2_omega_m1];
  
      /* - Matter part */

      double rho_g, rho_b, rho_cdm, rho_ur, rho_dipole_m1, rho_dipole_1, rho_dipole_2;
  
      rho_g = ppw2->pvecback[pba->index_bg_rho_g];
      rho_dipole_m1        =  rho_g*ppw2->I_1m[1];
      rho_dipole_1         =  rho_g*I_1_tilde(1);
      rho_dipole_2         =  rho_g*I_2_tilde(1);
  
      rho_b = ppw2->pvecback[pba->index_bg_rho_b];
      rho_dipole_m1        +=  rho_b*b(1,1,1);
      rho_dipole_1         +=  rho_b*pvec_sources1[ppt->index_qs_dipole_b];
      rho_dipole_2         +=  rho_b*pvec_sources2[ppt->index_qs_dipole_b];
  
      if (pba->has_cdm == _TRUE_) {
        rho_cdm = ppw2->pvecback[pba->index_bg_rho_cdm];
        rho_dipole_m1        +=  rho_cdm*cdm(1,1,1);
        rho_dipole_1         +=  rho_cdm*pvec_sources1[ppt->index_qs_dipole_cdm];
        rho_dipole_2         +=  rho_cdm*pvec_sources2[ppt->index_qs_dipole_cdm];
      }
  
      if (pba->has_ur == _TRUE_) {
        rho_ur = ppw2->pvecback[pba->index_bg_rho_ur];
        rho_dipole_m1        +=  rho_ur*N(1,1);
        rho_dipole_1         +=  rho_ur*N_1_tilde(1);
        rho_dipole_2         +=  rho_ur*N_2_tilde(1);
      }
      
  
      /* - Quadratic metric part */
  
      double vector_quad = 2 *  /* coming from the perturbative expansion */
        (
            2 * psi_1 * psi_2 * Hc  * (       k1_m[2]   +     k2_m[2] )
          - 2 * psi_1 * phi_2 * Hc  * (       k1_m[2]                 )
          - 2 * psi_2 * phi_1 * Hc  * (                 +     k2_m[2] )
          - 2 * phi_1 * phi_prime_2 * (       k1_m[2]   +   2*k2_m[2] )
          - 2 * phi_2 * phi_prime_1 * (     2*k1_m[2]   +     k2_m[2] )
          +     psi_1 * phi_prime_2 * (       k1_m[2]                 )
          +     psi_2 * phi_prime_1 * (                       k2_m[2] )
          /* Quadratic term arising from the tetrads */
          + 0.5 * a_sq * ( k1_m[2]/k1*rho_dipole_1*(psi_2+phi_2)
                         + k2_m[2]/k2*rho_dipole_2*(psi_1+phi_1) )
        );
  
  
      /* Since Hc = a*H, we have that Hc' = a*H' + a'*H */
      double Hc_prime = a*ppw2->pvecback[pba->index_bg_H_prime]
        + (a*Hc)*ppw2->pvecback[pba->index_bg_H]; 
    
      omega_m1_constraint = 2/(4*Hc*Hc - 4*Hc_prime + k_sq)
        * (-a_sq*rho_dipole_m1 - vector_quad);
  
    } // end of vector potential
    
    
    /* - Tensor potential */
    
    if (ppr2->compute_m[2] == _TRUE_) {
      
      gamma_m2 = y[ppw2->pv->index_pt2_gamma_m2];
      gamma_m2_prime = y[ppw2->pv->index_pt2_gamma_m2_prime];
      
    } 
      
  } // end of if(newtonian gauge)
  
  
  
  // ====================================================================================
  // =                               Synchronous gauge                                  =
  // ====================================================================================

  double alpha_prime, h_prime_prime, eta_prime_prime, h;
  
  if (ppt->gauge == synchronous) {

    if (ppr2->compute_m[0] == _TRUE_) {

      // /* alpha' = (h''+ 6 eta'')/2k^2 */
      // alpha_prime = pvecmetric[ppw2->index_mt2_alpha_prime];
      //   
      // /* h'' */
      // h_prime_prime = pvecmetric[ppw2->index_mt2_h_prime_prime];
      //   
      // /* eta'' = 1./6. * (2 k^2 alpha' - h'') */
      // // TODO:  Include the quadratic terms for eta_prime_prime
      // eta_prime_prime = 1./6. * (2.*k_sq*alpha_prime - h_prime_prime);
      // 
      // // TODO:  Include the quadratic terms of the continuity equation
      // h = -2. * delta_cdm;     
      //   
      // /* Synchronous to Netwonian gauge transformation for the potentials */
      // // TODO:  Include the quadratic terms of the gauge transformation
      // psi = (a * H * (pvecmetric[ppw2->index_mt2_h_prime] + 6. * pvecmetric[ppw2->index_mt2_eta_prime])/2./k_sq + alpha_prime);  
      // phi = y[ppw2->pv->index_pt2_eta]-a*H/2./k_sq*(pvecmetric[ppw2->index_mt2_h_prime] + 6. * pvecmetric[ppw2->index_mt2_eta_prime]);
    }
  } // end of synchronous gauge

  
  
  // ====================================================================================
  // =                            Matter domination limits                              =
  // ====================================================================================

  /* We test SONG metric and matter variables by making use of known analytical limits.
  Some of these limits are valid in matter domination only, so expect deviations as
  you approach today due to dark energy. */

  /* - Scalar modes in matter domination */

  double delta_cdm_analytical=0, theta_cdm_analytical=0, v_0_cdm_analytical=0, psi_analytical=0;
  
  if (ppr2->compute_m[0] == _TRUE_) {
      
    /* We take the delta and theta equations from eq. 45-46 of Bernardeau et al. 2002.
    The 2 factors come from the fact that we expand X = X0 + X1 + 1/2*X2. */
    double kernel_delta = 5/7. + 0.5*cosk1k2*(k1/k2+k2/k1) + 2/7.*cosk1k2*cosk1k2;
    delta_cdm_analytical = 2*kernel_delta*ppw2->delta_cdm_1*ppw2->delta_cdm_2;
  
    double kernel_theta = 3/7. + 0.5*cosk1k2*(k1/k2+k2/k1) + 4/7.*cosk1k2*cosk1k2;
    theta_cdm_analytical = 2*kernel_theta*ppw2->delta_cdm_1*ppw2->delta_cdm_2;
    v_0_cdm_analytical   = theta_cdm_analytical/k;
  
    /* The psi approximation comes from eq. 6 of Pitrou et al. 2008. We add a minus sign
    to match our potential. This is clearly related to Poisson equation. */
    double kernel_psi = k1_sq*k2_sq/(3/2.*Hc_sq*k_sq) * kernel_delta;
    psi_analytical = -2*kernel_psi*psi_1*psi_2;
  }


  /* - Vector potential in matter domination */

  /* Compute the limit for the vector potential in matter domination from eq. 2.6
  of Boubeker, Creminelli et al. 2009. The extra factor 2 comes from the fact that
  we expand X = X0 + X1 + 1/2*X2. */
  double omega_m1_analytical;

  if (ppr2->compute_m[1] == _TRUE_) {
    double kernel_omega_m1 =  4/(3*Hc*k_sq) * (k1_sq * k2_m[2] + k2_sq * k1_m[2]);
    // kernel_omega_m1 =  4/(3/tau*k_sq) * (k1_sq * k2_m[2] + k2_sq * k1_m[2]);
    omega_m1_analytical = 2 * kernel_omega_m1 * psi_1 * psi_2;
  }
  

  /* - Limits for the tensor modes */
  
  /* Compute the limit for the tensor potential in matter domination from eq. 2.7
  of Boubeker, Creminelli et al. 2009. */
  double gamma_m2_analytical;
  
  if (ppr2->compute_m[2] == _TRUE_) {    
    double kernel_gamma_m2 = ppw2->k1_ten_k2[4]/k_sq;
    double ktau = k*tau;
    double j1_ktau = sin(ktau)/(ktau*ktau) - cos(ktau)/ktau;
    /* Here we omit a factor 1/2 coming from the fact that our gamma is half the one in eq. 2.6
    of Boubeker, Creminelli et al. 2009, and a factor 2 coming from the perturbative expansion
    X~X^1+1/2*X^2 */
    gamma_m2_analytical = - 20 * (1/3. - j1_ktau/(k*tau)) * kernel_gamma_m2 * psi_1 * psi_2;    
  }


  /* - Experimental: include lambda */

  /* Extend the matter domination limits by including the effect of a cosmological
  constant. We do so by using the formulas in Mollerach, Harari & Matarrese (2004).
  This is not working yet, not sure whether I am making some mistake in copying the
  formulas. */
  double z = 1/a - 1;
  double Omega_l0 = 1 - Omega_m0;
  double Ez = sqrt(Omega_m0*pow(1+z,3) + Omega_l0);
  Omega_m = Omega_m0*pow(1+z,3)/(Ez*Ez);
  double Omega_l = Omega_l0/(Ez*Ez);
  /* Mollerach, Harari & Matarrese (2004) use the formula for the growth factor in eq. 29 of Carroll,
  Press & Turner (1992), which we define below, but we do not use. Here, instead, we set g=1 because,
  contrary to that reference, we use the numerical result for the first-order psi, which already
  includes exactly the suppression of growth from the presence of dark energy. */
  // double g = Omega_m / (pow(Omega_m,4/7.) - Omega_l + (1+Omega_m/2)*(1+Omega_l/70.));
  // double g_today = Omega_m0 / (pow(Omega_m0,4/7.) - Omega_l0 + (1+Omega_m0/2)*(1+Omega_l0/70.));
  // g /= g_today;
  double g = 1;
  double H0 = pba->H0;
  double f = pow(Omega_m,4/7.);
  double Fz = 2*g*g*Ez*f/(Omega_m0*H0*(1+z)*(1+z));
  if (ppr2->compute_m[1] == _TRUE_) {
    omega_m1_analytical *= Fz*Hc/2;
  }
  
  /* Debug of the analytical limits for m=1 and m=2 */
  // if ((ppr2->compute_m[1] == _TRUE_) && (ppr2->compute_m[2] == _TRUE_)) {
  //   if (ppw2->n_steps==1) {
  //     fprintf (stderr, "%12s %12s %12s %12s %12s %12s %12s %12s %12s %12s \n",
  //       "tau", "z", "frac_vec", "frac_ten", "rho_r/rho_m", "growth", "Fz*Hc/2", "Fz/tau/2", "Omega_m", "Omega_l");
  //   }
  //   else {
  //     fprintf (stderr, "%12g %12g %12g %12g %12g %12g %12g %12g %12g %12g \n",
  //       tau, z, 1-omega_m1_analytical/y[ppw2->pv->index_pt2_omega_m1],
  //       1-gamma_m2_analytical/y[ppw2->pv->index_pt2_gamma_m2],
  //       (pvecback[pba->index_bg_rho_g]+pvecback[pba->index_bg_rho_ur])/
  //       (pvecback[pba->index_bg_rho_cdm]+pvecback[pba->index_bg_rho_b]),
  //       g, Fz*Hc/2, Fz/tau/2, Omega_m, Omega_l);
  //   }
  // }


  // ====================================================================================
  // =                               Early universe limits                             =
  // ====================================================================================

  /* - Compute useful quantities in the tight coupling limit */
  
  class_call (perturb2_tca_variables(
                ppr,
                ppr2,
                pba,
                pth,
                ppt,
                ppt2,
                tau,
                y,
                ppw2),
    ppt2->error_message,
    error_message);


  /* - Adiabatic velocity at early times */

  /* Compute the common velocity of all matter species for adiabatic initial conditions.
  The formula is derived in sec. 5.4.1.1 of http://arxiv.org/abs/1405.2280 starting from
  the time-space (longitudinal) Einstein equation.  */

  double v_0_adiabatic = 0;

  if (ppr2->compute_m[0] == _TRUE_) {

    double L_quad = ppw2->pvec_quadsources[ppw2->index_qs2_phi_prime_longitudinal];
    v_0_adiabatic = 2*(k/Hc)*(psi - L_quad/Hc)
                         - ppw2->u_cdm_1[0]*(3*Omega_m*ppw2->delta_cdm_2 + 4*Omega_r*ppw2->delta_g_2)
                         - ppw2->u_cdm_2[0]*(3*Omega_m*ppw2->delta_cdm_1 + 4*Omega_r*ppw2->delta_g_1);
    v_0_adiabatic *= 1/(3*Omega_m + 4*Omega_r);
  }



  // ====================================================================================
  // =                                  Print to file                                   =
  // ====================================================================================

  /* Shortcut to the file where we shall print the transfer functions. */
  FILE * file_tr = ppt2->transfers_file;
  int index_print_tr = 1;
  
  /* Shortcut to the file where we shall print all the quadratic sources */
  FILE * file_qs = ppt2->quadsources_file;    
  int index_print_qs = 1;
  
  /* Choose how label & values should be formatted */
  char format_label[64] = "%18s(%02d) ";
  char format_value[64] = "%+22.11e ";
  char buffer[64];
  
  /* Print some info to file */
  if ((ppw2->n_steps==1) && (ppt2->perturbations2_verbose > 0)) {
    fprintf(file_tr, "%s%s", pba->info, ppw2->info);
    fprintf(file_qs, "%s%s", pba->info, ppw2->info);
  }
  

  // ------------------------------------------------------------------------------------
  // -                                 Time variables                                   -
  // ------------------------------------------------------------------------------------

  // conformal time
  if (ppw2->n_steps==1) {
    fprintf(file_tr, format_label, "tau", index_print_tr++);
    fprintf(file_qs, format_label, "tau", index_print_qs++);
  }
  else {
    fprintf(file_tr, format_value, tau);
    fprintf(file_qs, format_value, tau);
  }
  // scale factor
  if (ppw2->n_steps==1) {
    fprintf(file_tr, format_label, "a", index_print_tr++);
    fprintf(file_qs, format_label, "a", index_print_qs++);
  }
  else {
    fprintf(file_tr, format_value, a);
    fprintf(file_qs, format_value, a);
  }
  // y = log10(a/a_eq)
  if (ppw2->n_steps==1) {
    fprintf(file_tr, format_label, "y", index_print_tr++);
    fprintf(file_qs, format_label, "y", index_print_qs++);
  }
  else {
    fprintf(file_tr, format_value, Y);
    fprintf(file_qs, format_value, Y);
  }
  

  // ------------------------------------------------------------------------------------
  // -                                Newtonian gauge                                   -
  // ------------------------------------------------------------------------------------

  if (ppt->gauge == newtonian) {    

    /* Scalar potentials */
    if (ppr2->compute_m[0] == _TRUE_) {
      // psi
      if (ppw2->n_steps==1) {
       fprintf(file_tr, format_label, "psi", index_print_tr++);
       fprintf(file_qs, format_label, "psi", index_print_qs++);
      }
      else {
       fprintf(file_tr, format_value, psi);
       fprintf(file_qs, format_value, pvec_quadsources[ppw2->index_qs2_psi]);
      }
      // psi_prime
      if (ppt2->has_isw == _TRUE_) {
        if (ppw2->n_steps==1) {
         fprintf(file_tr, format_label, "psi'", index_print_tr++);
         fprintf(file_qs, format_label, "psi'", index_print_qs++);
        }
        else {
         fprintf(file_tr, format_value, psi_prime);
         fprintf(file_qs, format_value, pvec_quadsources[ppw2->index_qs2_psi_prime]);
        }
      }
      // phi
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, "phi", index_print_tr++);
      else fprintf(file_tr, format_value, phi);
      // phi_prime_poisson
      if (ppw2->n_steps==1) {
       fprintf(file_tr, format_label, "phi'tt", index_print_tr++);
       fprintf(file_qs, format_label, "phi'tt", index_print_qs++);
      }
      else {
       fprintf(file_tr, format_value, pvecmetric[ppw2->index_mt2_phi_prime_poisson]);
       fprintf(file_qs, format_value, pvec_quadsources[ppw2->index_qs2_phi_prime_poisson]);
      }
      // phi_prime_longitudinal
      if (ppw2->n_steps==1) {
       fprintf(file_tr, format_label, "phi'lg", index_print_tr++);
       fprintf(file_qs, format_label, "phi'lg", index_print_qs++);
      }
      else {
       fprintf(file_tr, format_value, pvecmetric[ppw2->index_mt2_phi_prime_longitudinal]);
       fprintf(file_qs, format_value, pvec_quadsources[ppw2->index_qs2_phi_prime_longitudinal]);
      }
      // psi_analytical
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, "psi_an", index_print_tr++);
      else fprintf(file_tr, format_value, psi_analytical);
    }  
    /* Vector potentials */
    if (ppr2->compute_m[1] == _TRUE_) {
      // omega_m1
      if (ppw2->n_steps==1) {
       fprintf(file_tr, format_label, "omega_m1", index_print_tr++);
       fprintf(file_qs, format_label, "omega_m1'", index_print_qs++);
      }
      else {
       fprintf(file_tr, format_value, y[ppw2->pv->index_pt2_omega_m1]);
       fprintf(file_qs, format_value, pvec_quadsources[ppw2->index_qs2_omega_m1_prime]);
      }
      // omega_m1_constraint
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, "omega_m1_c", index_print_tr++);
      else fprintf(file_tr, format_value, omega_m1_constraint);
      // omega_m1_analytical
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, "omega_m1_an", index_print_tr++);
      else fprintf(file_tr, format_value, omega_m1_analytical);
    }
    /* Tensor potentials */
    if (ppr2->compute_m[2] == _TRUE_) {
      // gamma_m2
      if (ppw2->n_steps==1) {
       fprintf(file_tr, format_label, "gamma_m2", index_print_tr++);
       fprintf(file_qs, format_label, "gamma_m2''", index_print_qs++);
      }
      else {
       fprintf(file_tr, format_value, y[ppw2->pv->index_pt2_gamma_m2]);
       fprintf(file_qs, format_value, pvec_quadsources[ppw2->index_qs2_gamma_m2_prime_prime]);
      }
      // gamma_m2_analytical
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, "gamma_m2_an", index_print_tr++);
      else fprintf(file_tr, format_value, gamma_m2_analytical);
      // gamma_m2_prime
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, "gamma_m2'", index_print_tr++);
      else fprintf(file_tr, format_value, y[ppw2->pv->index_pt2_gamma_m2_prime]);
    }
  
  } // end of if(newtonian)
  
  
  // ------------------------------------------------------------------------------------
  // -                            Matter domination limits                              -
  // ------------------------------------------------------------------------------------

  if (pba->has_cdm == _TRUE_ ) {
    if (ppr2->compute_m[0] == _TRUE_) {
      // delta_cdm_analytical
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, "deltacdm_an", index_print_tr++);
      else fprintf(file_tr, format_value, delta_cdm_analytical);
      // v_0_cdm_analytical
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, "v_0_cdm_an", index_print_tr++);
      else fprintf(file_tr, format_value, v_0_cdm_analytical);
    }  
  }
  
  
  // ------------------------------------------------------------------------------------
  // -                             Early universe limits                                -
  // ------------------------------------------------------------------------------------

  /* Adiabatic velocity */
  if (ppr2->compute_m[0] == _TRUE_) {
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, "v_0_adiab", index_print_tr++);
    else fprintf(file_tr, format_value, v_0_adiabatic);
  }  
  
  // #define V_g(m) I(1,m)*0.25 - delta_g_1*(-k2_m[m+1]*v_g_2) - delta_g_2*(-k1_m[m+1]*v_g_1)
  // #define V_b(m) b(1,1,m)/3. - delta_b_1*(-k2_m[m+1]*v_b_2) - delta_b_2*(-k1_m[m+1]*v_b_1)

  // *** magnetic field
  if (ppt2->has_magnetic_field == _TRUE_) {
  	if (ppr2->compute_m[1] == _TRUE_) {
  		if (ppw2->n_steps==1) fprintf(file_tr, format_label, "mag", index_print_tr++);
  		else fprintf(file_tr, format_value, mag(1,1));
  	}
  	if (ppr2->compute_m[1] == _TRUE_) {
  		if (ppw2->n_steps==1) fprintf(file_tr, format_label, "dipsource", index_print_tr++);
  		else fprintf(file_tr, format_value, 
  		/* ( I(1,1)*0.25 - delta_g_1*(-k2_m[1+1]*v_g_2) - delta_g_2*(-k1_m[1+1]*v_g_1)
  		 - (b(1,1,1)/3. - delta_b_1*(-k2_m[1+1]*v_b_2) - delta_b_2*(-k1_m[1+1]*v_b_1)) )*/
  			0.
  		 );
  	}
  	
  	if (ppr2->compute_m[1] == _TRUE_) {
  		if (ppw2->n_steps==1) fprintf(file_tr, format_label, "baryon velocity", index_print_tr++);
  		else fprintf(file_tr, format_value,  0.);
  	}
  	
  	if (ppr2->compute_m[1] == _TRUE_) {
  		if (ppw2->n_steps==1) fprintf(file_tr, format_label, "rho_g", index_print_tr++);
  		else fprintf(file_tr, format_value,  pvecback[pba->index_bg_rho_g]  );
  	}
  }

  /* Velocity slip (TCA1) */
  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "U_slip_tca1_m%d", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->U_slip_tca1[m]);
  }
  /* Photon velocity (TCA1) */
  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "u_g_tca1_m%d", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->u_g_tca1[m]);
  }
  /* Photon dipole (TCA1) */
  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "I_1_%d_tca1", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->I_1m_tca1[m]);
  }
  /* Dipole collision term (TCA1) */
  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "C_1_%d_tca1", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->C_1m_tca1[m]);
  }
  /* Dipole collision term (numeric) */
  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "C_1_%d", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->C_1m[m]);
  }
  /* Photon quadrupole (TCA0) */
  for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "I_2_%d_tca0", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->I_2m_tca0[m]);    
  }
  /* Photon quadrupole (TCA1) */
  for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "I_2_%d_tca1", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->I_2m_tca1[m]);    
  }
  /* Photon shear (TCA1) */
  for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "shear_g_m%d_tca1", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->shear_g_tca1[m]);    
  }
  /* E-modes quadrupole (TCA1) */
  if (ppt2->has_polarization2 == _TRUE_) {
    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppr2->m[index_m];
      sprintf(buffer, "E_2_%d_tca1", m);
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
      else fprintf(file_tr, format_value, ppw2->E_2m_tca1[m]);
    }
  }
  /* B-modes quadrupole (TCA1) */
  if (ppt2->has_polarization2 == _TRUE_) {
    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppr2->m[index_m];
      sprintf(buffer, "B_2_%d_tca1", m);
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
      else fprintf(file_tr, format_value, ppw2->B_2m_tca1[m]);
    }
  }
  /* Pi factor (TCA1) */
  if (ppt2->has_polarization2 == _TRUE_) {
    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppr2->m[index_m];
      sprintf(buffer, "pi_m%d", m);
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
      else fprintf(file_tr, format_value, 0.1 * (ppw2->I_2m[m] - sqrt_6*ppw2->E_2m[m]));
      sprintf(buffer, "pi_m%d_tca1", m);
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
      else fprintf(file_tr, format_value, ppw2->Pi_tca1[m]);
    }
  }

  
  // ------------------------------------------------------------------------------------
  // -                                  Fluid limit                                     -
  // ------------------------------------------------------------------------------------

  
  /* - Baryon fluid limit */

  if (ppr2->compute_m[0] == _TRUE_) {
    // delta_g_adiab
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, "delta_g_ad", index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->delta_g_adiab);
    // delta_b
    if (ppw2->n_steps==1)  fprintf(file_tr, format_label, "delta_b", index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->delta_b);
    // pressure_b 
    if (ppw2->n_steps==1)  fprintf(file_tr, format_label, "pressure_b", index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->pressure_b);
  }
  // velocity_b[m]
  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    int m = ppt2->m[index_m];
    sprintf(buffer, "vel_b_m%d", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->u_b[m]);
  }
  // velocity_b_prime[m]
  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "vel_b_m%d_prime", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, db(1,1,m)/3
      - ppw2->delta_b_1_prime*ppw2->u_b_2[m] - ppw2->delta_b_2_prime*ppw2->u_b_1[m]
      - ppw2->delta_b_1*ppw2->u_b_2_prime[m] - ppw2->delta_b_2*ppw2->u_b_1_prime[m]);
  }
  // shear_b[m]
  for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "shear_b_m%d", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->shear_b[m]);
  }  

  /* - CDM fluid limit */
  
  if (pba->has_cdm == _TRUE_ ) {
    if (ppr2->compute_m[0] == _TRUE_) {
      // delta_cdm
      if (ppw2->n_steps==1)  fprintf(file_tr, format_label, "delta_cdm", index_print_tr++);
      else fprintf(file_tr, format_value, ppw2->delta_cdm);
      // pressure_cdm 
      if (ppw2->n_steps==1)  fprintf(file_tr, format_label, "pressure_cdm", index_print_tr++);
      else fprintf(file_tr, format_value, ppw2->pressure_cdm);
    }
    // velocity_cdm[m]
    if (ppt->gauge != synchronous) {
      for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
        int m = ppt2->m[index_m];
        sprintf(buffer, "vel_cdm_m%d", m);
        if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
        else fprintf(file_tr, format_value, ppw2->u_cdm[m]);
      }
    }  
    // shear_cdm[m]
    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppr2->m[index_m];
      sprintf(buffer, "shear_cdm_m%d", m);
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
      else fprintf(file_tr, format_value, ppw2->shear_cdm[m]);    
    }  
  }
   
  /* - Photon fluid limit */

  if (ppr2->compute_m[0] == _TRUE_) {
    // delta_g
    if (ppw2->n_steps==1)  fprintf(file_tr, format_label, "delta_g", index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->delta_g);
    // pressure_g 
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, "pressure_g", index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->pressure_g);
  }
  // velocity_g[m]
  for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "vel_g_m%d", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->u_g[m]);
  }
  // shear_g[m]
  for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
    int m = ppr2->m[index_m];
    sprintf(buffer, "shear_g_m%d", m);
    if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
    else fprintf(file_tr, format_value, ppw2->shear_g[m]);
  }
  
  /* - Neutrino fluid limit */
  
  if (pba->has_ur == _TRUE_) {
    if (ppr2->compute_m[0] == _TRUE_) {
      // delta_ur
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, "delta_ur", index_print_tr++);
      else fprintf(file_tr, format_value, ppw2->delta_ur);
      // pressure_ur 
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, "pressure_ur", index_print_tr++);
      else fprintf(file_tr, format_value, ppw2->pressure_ur);
    }  
    // velocity_ur[m]
    for (int index_m=0; index_m <= ppr2->index_m_max[1]; ++index_m) {
      int m = ppr2->m[index_m];
      sprintf(buffer, "vel_ur_m%d", m);
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
      else fprintf(file_tr, format_value, ppw2->u_ur[m]);
    }
    // shear_ur[m]
    for (int index_m=0; index_m <= ppr2->index_m_max[2]; ++index_m) {
      int m = ppr2->m[index_m];
      sprintf(buffer, "shear_ur_m%d", m);
      if (ppw2->n_steps==1) fprintf(file_tr, format_label, buffer, index_print_tr++);
      else fprintf(file_tr, format_value, ppw2->shear_ur[m]);
    }
  }
  

  // ------------------------------------------------------------------------------------
  // -                                   Multipoles                                     -
  // ------------------------------------------------------------------------------------

  /* - Baryon multipoles */
  
  for (int n=0; n <= ppw2->pv->n_max_b; ++n) {
    for (int l=0; l <= ppw2->pv->l_max_b; ++l) {
      if ((l!=n) && (l!=0)) continue;
      if ((l==0) && (n%2!=0)) continue;
      if (ppt2->has_perfect_baryons == _TRUE_)
        if ((n>1)||(l>1)) continue;
      
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
        int m = ppr2->m[index_m];
        sprintf(buffer, "b_%d_%d_%d", n, l, m);
        if (ppw2->n_steps==1) {
          fprintf(file_tr, format_label, buffer, index_print_tr++);
          fprintf(file_qs, format_label, buffer, index_print_qs++);
        }
        else {
          fprintf(file_tr, format_value, b(n,l,m));
          fprintf(file_qs, format_value, db_qs2(n,l,m));
        }
      }
    }
  }

  /* - CDM multipoles */
  
  if (pba->has_cdm == _TRUE_) {
    for (int n=0; n <= ppw2->pv->n_max_cdm; ++n) {
      for (int l=0; l <= ppw2->pv->l_max_cdm; ++l) {
        if ((l!=n) && (l!=0)) continue;
        if ((l==0) && (n%2!=0)) continue;
        if (ppt2->has_perfect_cdm == _TRUE_)
          if ((n>1)||(l>1)) continue;

        for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
          int m = ppr2->m[index_m];
          sprintf(buffer, "cdm_%d_%d_%d", n, l, m);
          if (ppw2->n_steps==1) {
            fprintf(file_tr, format_label, buffer, index_print_tr++);
            fprintf(file_qs, format_label, buffer, index_print_qs++);
          }
          else {
            fprintf(file_tr, format_value, cdm(n,l,m));
            fprintf(file_qs, format_value, dcdm_qs2(n,l,m));
          }
        }
      }
    }
  }

  /* - Photon temperature multipoles */
  
  int l_max_g = MIN(ppw2->l_max_g, ppt2->l_max_debug);
  
  for (int l=0; l<=l_max_g; ++l) {
    for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
      int m = ppr2->m[index_m];
      sprintf(buffer, "I_%d_%d", l, m);
      if (ppw2->n_steps==1) {
        fprintf(file_tr, format_label, buffer, index_print_tr++);
        fprintf(file_qs, format_label, buffer, index_print_qs++);
      }
      else {
        if (l==1)
          fprintf(file_tr, format_value, ppw2->I_1m[m]);
        else if (l==2)
          fprintf(file_tr, format_value, ppw2->I_2m[m]);
        else
          fprintf(file_tr, format_value, I(l,m));
        fprintf(file_qs, format_value, dI_qs2(l,m));
      }
    }
  }
  
  /* - Photon polarisation multipoles */
  
  if (ppt2->has_polarization2 == _TRUE_) {
  
    int l_max_pol_g = MIN(ppw2->l_max_pol_g, ppt2->l_max_debug);
  
    if (ppt2->tight_coupling_approximation != tca2_none)
      l_max_g = MIN (l_max_g,1);
  
    /* E-mode polarization */
    for (int l=2; l<=l_max_pol_g; ++l) {
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
        int m = ppr2->m[index_m];
        sprintf(buffer, "E_%d_%d", l, m);
        if (ppw2->n_steps==1) {
          fprintf(file_tr, format_label, buffer, index_print_tr++);
          fprintf(file_qs, format_label, buffer, index_print_qs++);
        }
        else {
          if (l==2)
            fprintf(file_tr, format_value, ppw2->E_2m[m]);
          else
            fprintf(file_tr, format_value, E(l,m));
          fprintf(file_qs, format_value, dE_qs2(l,m));
        }
      }
    }
   
    /* B-mode polarization */
    for (int l=2; l<=l_max_pol_g; ++l) {
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
        int m = ppr2->m[index_m];
        sprintf(buffer, "B_%d_%d", l, m);
        if (ppw2->n_steps==1) {
         fprintf(file_tr, format_label, buffer, index_print_tr++);
         fprintf(file_qs, format_label, buffer, index_print_qs++);
        }
        else {
          if (l==2)
            fprintf(file_tr, format_value, ppw2->B_2m[m]);
          else
            fprintf(file_tr, format_value, B(l,m));
         fprintf(file_qs, format_value, dB_qs2(l,m));
        }
      }
    }
   
  } // end of if(has_polarization2)
  
  /* - Neutrino multipoles */
  
  if (pba->has_ur == _TRUE_) {
  
    int l_max_ur = MIN(ppw2->l_max_ur, ppt2->l_max_debug);
  
    for (int l=0; l<=l_max_ur; ++l) {
      for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
        int m = ppr2->m[index_m];
        sprintf(buffer, "N_%d_%d", l, m);
        if (ppw2->n_steps==1) {
         fprintf(file_tr, format_label, buffer, index_print_tr++);
         fprintf(file_qs, format_label, buffer, index_print_qs++);
        }
        else {
         fprintf(file_tr, format_value, N(l,m));
         fprintf(file_qs, format_value, dN_qs2(l,m));
        }
      }
    }
  }  // end of if(has_ur)
  
  
  // ------------------------------------------------------------------------------------
  // -                             Background & misc                                    -
  // ------------------------------------------------------------------------------------
  
  sprintf(buffer, "kappa_dot");
  if (ppw2->n_steps==1)
   fprintf(file_tr, format_label, buffer, index_print_tr++);
  else
   fprintf(file_tr, format_value, kappa_dot);
  
  sprintf(buffer, "exp_m_kappa");
  if (ppw2->n_steps==1)
   fprintf(file_tr, format_label, buffer, index_print_tr++);
  else
   fprintf(file_tr, format_value, pvecthermo[pth->index_th_exp_m_kappa]);
  
  sprintf(buffer, "g");
  if (ppw2->n_steps==1)
   fprintf(file_tr, format_label, buffer, index_print_tr++);
  else
   fprintf(file_tr, format_value, pvecthermo[pth->index_th_g]);
  
  sprintf(buffer, "H");
  if (ppw2->n_steps==1)
   fprintf(file_tr, format_label, buffer, index_print_tr++);
  else
   fprintf(file_tr, format_value, pvecback[pba->index_bg_H]);
  
  sprintf(buffer, "Hc");
  if (ppw2->n_steps==1)
   fprintf(file_tr, format_label, buffer, index_print_tr++);
  else
   fprintf(file_tr, format_value, a*pvecback[pba->index_bg_H]);


  // ------------------------------------------------------------------------------------
  // -                              Source functions                                    -
  // ------------------------------------------------------------------------------------

  /* Printing of sources is disabled since CLASS v2. The reason is
  that prior to v2 this function was called when the differential
  system reached one of the time steps in ppt2->tau_sampling, which
  coincide to the time steps where the sources are computed. Now, 
  this function is called at the end of all time steps in the 
  differential system, meaning that there is now way to access
  the sources other than calling here a modified version of
  perturb2_sources() that does not take index_tau as an input. */
  
  // // *** Photon temperature sources
  // int l_max_los_t = MIN(ppr2->l_max_los_t, ppt2->l_max_debug);
  //
  // for (int l=0; l<=l_max_los_t; ++l) {
  //   for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
  //     int m = ppr2->m[index_m];
  //     sprintf(buffer, "I_%d_%d", l, m);
  //     if (ppw2->n_steps==1) {
  //      fprintf(file_tr, format_label, buffer, index_print_tr++);
  //     }
  //     else {
  //      fprintf(file_tr, format_value, sources(ppt2->index_tp2_T+lm(l,m))/kappa_dot);
  //     }
  //   }
  // }
  //
  // // *** Photon E-mode polarisation sources
  // int l_max_los_p = MIN(ppr2->l_max_los_p, ppt2->l_max_debug);
  //
  // for (int l=2; l<=l_max_los_p; ++l) {
  //   for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
  //     int m = ppr2->m[index_m];
  //     sprintf(buffer, "E_%d_%d", l, m);
  //     if (ppw2->n_steps==1) {
  //      fprintf(file_tr, format_label, buffer, index_print_tr++);
  //     }
  //     else {
  //      fprintf(file_tr, format_value, sources(ppt2->index_tp2_E+lm(l,m))/kappa_dot);
  //     }
  //   }
  // }
  //
  // // *** Photon B-mode polarisation sources
  // for (int l=2; l<=l_max_los_p; ++l) {
  //   for (int index_m=0; index_m <= ppr2->index_m_max[l]; ++index_m) {
  //     int m = ppr2->m[index_m];
  //     if (m==0) continue;
  //     sprintf(buffer, "B_%d_%d", l, m);
  //     if (ppw2->n_steps==1) {
  //      fprintf(file_tr, format_label, buffer, index_print_tr++);
  //     }
  //     else {
  //      fprintf(file_tr, format_value, sources(ppt2->index_tp2_B+lm(l,m))/kappa_dot);
  //     }
  //   }
  // }
  
  fprintf(file_tr, "\n");
  fprintf(file_qs, "\n");
  
  return _SUCCESS_;

}








int perturb2_quadratic_sources_at_tau (
        struct precision * ppr,
        struct precision2 * ppr2,
        struct perturbs * ppt,
        struct perturbs2 * ppt2,
        double tau,
        int what_to_interpolate, /**< input: which quadratic sources should we compute? Options are
                                 documented in enum quadratic_source_interpolation */
        struct perturb2_workspace * ppw2
        )
{
  
  /* Linear interpolation */
  if (ppr->quadsources_time_interpolation == linear_interpolation) {

    class_call(perturb2_quadratic_sources_at_tau_linear (
              ppt,
              ppt2,
              tau,
              what_to_interpolate,
              ppw2
              ),
          ppt2->error_message,
          ppt2->error_message);

  }
  /* Cubic spline interpolation */
  else if (ppr->quadsources_time_interpolation == cubic_interpolation) {

    class_call(perturb2_quadratic_sources_at_tau_spline (
              ppt,
              ppt2,
              tau,
              what_to_interpolate,
              ppw2
              ),
          ppt2->error_message,
          ppt2->error_message);

  } // end of if(ppr->quadsources_time_interpolation)
  
  
  return _SUCCESS_;
  
}



/**
  * Function that interpolates the quadratic sources at a time tau for a specific wavemode
  * set (index_k1, index_k2, index_k3), and for all types, using linear interpolation.
  *
  * This function assumes that the quadratic sources are stored in
  * ppw2->quadsources_table[ppt2->index_qs2][index_tau]
  * and it fills
  * ppw2->pvec_quadsources[ppt2->index_qs2]
  *
  */
int perturb2_quadratic_sources_at_tau_linear(
        struct perturbs * ppt,
        struct perturbs2 * ppt2,
        double tau,
        int what_to_interpolate,
        struct perturb2_workspace * ppw2
        )
{

  class_test (((tau>ppt->tau_sampling_quadsources[ppt->tau_size_quadsources-1])
    || (tau<ppt->tau_sampling_quadsources[0])),
    ppt2->error_message,
    "you requested a time which is not contained in the interpolation table.");

  int index_tau; /* index at the left of tau in ppt->tau_sampling_quadsources */
  double tau_right; /* time at the right of tau */
  double step; /* time step, only for lin_tau_sampling and log_tau_sampling */


  // ====================================================================================
  // =                             Find time index in the table                         =
  // ====================================================================================
  
  /* When the time-sampling is linear or logarithmic, we can obtain the
  position of tau in a straightforward way */
  if (ppt->has_custom_timesampling_for_quadsources == _TRUE_) {

    if (ppt->custom_tau_mode_quadsources == lin_tau_sampling) {
      step = ppt->custom_tau_step_quadsources;
      index_tau = (int)((tau - ppt->custom_tau_ini_quadsources)/step);
      tau_right = ppt->tau_sampling_quadsources[index_tau+1];
    }

    else if (ppt->custom_tau_mode_quadsources == log_tau_sampling) {
      double log_step = ppt->custom_tau_step_quadsources;
      index_tau = (int)((log(tau) - ppt->custom_log_tau_ini_quadsources)/log_step);
      tau_right = ppt->tau_sampling_quadsources[index_tau+1];
      step = tau_right - ppt->tau_sampling_quadsources[index_tau];
    }
  }
  /* For a general time-sampling, we resort to bisection */
  else {
    
    int inf = 0;
    int sup = ppt->tau_size_quadsources - 1;
    int mid, i;
    double * tau_vec = ppt->tau_sampling_quadsources;

    if (tau_vec[inf] < tau_vec[sup]){
      while (sup-inf > 1) {
        mid = (int)(0.5*(inf+sup));
        if (tau < tau_vec[mid])
          sup = mid;
        else
          inf = mid;
      }
    }
    else {
      while (sup-inf > 1) {
        mid = (int)(0.5*(inf+sup));
        if (tau > tau_vec[mid])
          sup = mid;
        else
          inf = mid;
      }
    }

    index_tau = mid-1;
    tau_right = tau_vec[index_tau+1];
    step = tau_right - tau_vec[index_tau]; 
  }


  // ====================================================================================
  // =                               Interpolate the table                              =
  // ====================================================================================

  double **table, *result;
  int size;

  if (what_to_interpolate == interpolate_total) {
    table = ppw2->quadsources_table;
    result = ppw2->pvec_quadsources;
    size = ppw2->qs2_size;
  }
  else if (what_to_interpolate == interpolate_collision) {
    table = ppw2->quadcollision_table;
    result = ppw2->pvec_quadcollision;    
    size = ppw2->qs2_size;
  }
  else {
    class_stop (ppt2->error_message,
      "what_to_interpolate=%d not supported",
      what_to_interpolate);
  }

  for (int index_qs2=0; index_qs2<size; ++index_qs2) {

    double a = (tau_right-tau)/step;
    double source_left = table[index_qs2][index_tau];
    double source_right = table[index_qs2][index_tau+1];
    result[index_qs2] = a*source_left + (1-a)*source_right;
  }
  
  return _SUCCESS_;
}







int perturb2_quadratic_sources_at_tau_spline (
        struct perturbs * ppt,
        struct perturbs2 * ppt2,
        double tau,
        int what_to_interpolate,
        struct perturb2_workspace * ppw2
        )
{
  
  double **table, **dd_table, *result;
  int size, dump;

  if (what_to_interpolate == interpolate_total) {
    table = ppw2->quadsources_table;
    dd_table = ppw2->dd_quadsources_table;
    result = ppw2->pvec_quadsources;
    size = ppw2->qs2_size;
  }
  else if (what_to_interpolate == interpolate_collision) {
    table = ppw2->quadcollision_table;
    dd_table = ppw2->dd_quadcollision_table;
    result = ppw2->pvec_quadcollision;    
    size = ppw2->qs2_size;
  }
  else {
    class_stop (ppt2->error_message,
      "what_to_interpolate=%d not supported",
      what_to_interpolate);
  }

  class_call (spline_sources_interpolate_two_levels (
           ppt->tau_sampling_quadsources,
           ppt->tau_size_quadsources,
           table,
           dd_table,
           size,
           tau,
           &dump,
           result,
           size,
           ppt2->error_message
           ),
        ppt2->error_message,
        ppt2->error_message);  

  return _SUCCESS_; 

}



/**
 * Save the second-order line of sight sources to disk for a given k1 value.
 * 
 * The sources will be saved to the file in ppt2->sources_paths[index_k1].
 */
int perturb2_store_sources_to_disk(
        struct perturbs2 * ppt2,
        int index_k1
        )
{

  /* Open file for writing */
  class_open (ppt2->sources_files[index_k1], ppt2->sources_paths[index_k1], "wb", ppt2->error_message);

  /* Running indexes */
  int index_type, index_k2, index_k3;

  /* Print some debug */
  if (ppt2->perturbations2_verbose > 1)
    printf("     \\ writing sources for index_k1=%d ...\n", index_k1);
  
  for (index_type = 0; index_type < ppt2->tp2_size; ++index_type) {
  
    for (index_k2 = 0; index_k2 <= index_k1; ++index_k2) {

      int k3_size = ppt2->k3_size[index_k1][index_k2];

        /* Write a chunk with all the time values for this set of (type,k1,k2,k3) */
        if (k3_size > 0)
          fwrite(
                ppt2->sources[index_type][index_k1][index_k2],
                sizeof(double),
                ppt2->tau_size*k3_size,
                ppt2->sources_files[index_k1]
                );

    } // end of for (index_k2)
    
  } // end of for (index_type)
  
  /* Close file */
  fclose(ppt2->sources_files[index_k1]);

  // if (ppt2->perturbations2_verbose > 1)
  //   printf(" Done.\n");

  /* Write information on the status file */
  // class_open (ppt2->sources_status_file, ppt2->sources_status_path, "a+b", ppt2->error_message);
    
  return _SUCCESS_; 
  
}

#undef sources

//* This should not be here and be moved to class eventually. Also bispline might be the way to go here or some other fancy method. For now I am just using linear interpolation in k, followed by whatever is selected for eta */

int perturb_song_sources_at_tau_and_k (  
             struct precision * ppr,
             struct perturbs * ppt,
             int index_mode,
             int index_ic,
             double k,
             double tau,
             short intermode,
             int * last_index,
             double * psource){
  
  int index_k; /*Index left of k*/
  
  int inf = 0;
  int sup = ppt->k_size[index_mode] - 1;
  int mid, i;
  double * k_vec = ppt->k[index_mode];

    if (k_vec[inf] < k_vec[sup]){

      while (sup-inf > 1) {

        mid = (int)(0.5*(inf+sup));

        if (k < k_vec[mid])
          sup = mid;
        else
          inf = mid;
      }
    }
    else {
      while (sup-inf > 1) {

        mid = (int)(0.5*(inf+sup));

        if (k > k_vec[mid])
          sup = mid;
        else
          inf = mid;
      }
    }

    index_k = mid-1;
    double k_right = k_vec[index_k+1];
    double step = k_right - k_vec[index_k];
		double a = (k_right - k)/step;  
  	int qs_size = ppt->qs_size[index_mode];
  	
  	double * source_left;
  	double * source_right;
  	
  	class_alloc(source_left,qs_size*sizeof(double),ppt->error_message);
  	class_alloc(source_right,qs_size*sizeof(double),ppt->error_message);
  	
  	class_call (perturb_song_sources_at_tau (
                ppr,
                ppt,
                index_mode,
                index_ic,
                index_k,
                tau,
                ppt->qs_size[ppt->index_md_scalars],
                intermode,
                last_index,
                source_left),
    	ppt->error_message,
    	ppt->error_message);

    class_call (perturb_song_sources_at_tau (
                ppr,
                ppt,
                index_mode,
                index_ic,
                index_k+1,
                tau,
                ppt->qs_size[ppt->index_md_scalars],
                intermode,
                last_index,
                source_right),
    	ppt->error_message,
    	ppt->error_message);
                
   	for (int index_type=0; index_type<qs_size; ++index_type) {
 	
                      
    	psource[index_type] = a*source_left[index_type] + (1-a)*source_right[index_type];
   
  	}
  	
  	free(source_left);
  	free(source_right);
  	return _SUCCESS_;

}
