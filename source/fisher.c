/** @file fisher.c documented Fisher matrix module for second-order perturbations
 *
 * Guido W Pettinari, 19.07.2012
 *
 */

#include "fisher.h"


int fisher_init (
     struct precision * ppr,
     struct background * pba,
     struct thermo * pth,
     struct perturbs * ppt,
     struct bessels * pbs,
     struct transfers * ptr,
     struct primordial * ppm,
     struct spectra * psp,
     struct lensing * ple,
     struct bispectra * pbi,
     struct fisher * pfi
     )
{


  /* Check whether we need to compute spectra at all */  
  if (pfi->has_fisher == _FALSE_) {
  
    if (pfi->fisher_verbose > 0)
      printf("No forecasts requested. Fisher module skipped.\n");
  
    return _SUCCESS_;
  }
  else {
    if (pfi->fisher_verbose > 0)
      printf("Computing Fisher matrix\n");
  }


  // =========================================================
  // =                    Preparations                       =
  // =========================================================
  
  /* Initialize indices & arrays in the Fisher structure */
  
  class_call (fisher_indices(ppr,pba,ppt,pbs,ptr,ppm,psp,ple,pbi,pfi),
    pfi->error_message,
    pfi->error_message);
    

  /* Load intrinsic bispectra from disk if they were not already computed. Note that we are only loading the
  intrinsic bispectra, since the primary ones are very quick to recompute. */

  if (pbi->load_bispectra_from_disk == _TRUE_) {
    
    for (int index_bt = 0; index_bt < pbi->bt_size; ++index_bt)   
      if ((pbi->bispectrum_type[index_bt] == non_separable_bispectrum)
       || (pbi->bispectrum_type[index_bt] == intrinsic_bispectrum))
        class_call (bispectra_load_from_disk (
                      pbi,
                      index_bt),
          pbi->error_message,
          pfi->error_message);
  }


  // ======================================================================
  // =                   Compute noise power spectrum                     =
  // ======================================================================
  
  class_call (fisher_noise(ppr,pba,ppt,pbs,ptr,ppm,psp,ple,pbi,pfi),
    pfi->error_message,
    pfi->error_message);
  
  

  // ====================================================================
  // =                      Compute cross C_l's                         =
  // ====================================================================
    
  class_call (fisher_cross_cls(ppr,pba,ppt,pbs,ptr,ppm,psp,ple,pbi,pfi),
    pfi->error_message,
    pfi->error_message);
  
  
  
  // =====================================================================
  // =                      Prepare interpolation mesh                   =
  // =====================================================================
  
  if ((pfi->bispectra_interpolation == mesh_interpolation)
  || (pfi->bispectra_interpolation == mesh_interpolation_2d)
  && (pfi->has_only_analytical_bispectra == _FALSE_))
    
    class_call (fisher_create_interpolation_mesh(ppr,pba,ppt,pbs,ptr,ppm,psp,ple,pbi,pfi),
      pfi->error_message,
      pfi->error_message);
  
  
  
  // ====================================================================
  // =                       Compute Fisher matrix                      =
  // ====================================================================
  
  class_call (fisher_compute(ppr,pba,ppt,pbs,ptr,ppm,psp,ple,pbi,pfi),
    pfi->error_message,
    pfi->error_message);

  return _SUCCESS_;

}











/**
 * This routine frees all the memory space allocated by fisher_init().
 *
 * @param pfi Input: pointer to fisher structure (which fields must be freed)
 * @return the error status
 */

int fisher_free(
     struct bispectra * pbi,
     struct fisher * pfi
     )
{

  if (pfi->has_fisher == _TRUE_) {

    for (int X = 0; X < pbi->bf_size; ++X) {
      for (int Y = 0; Y < pbi->bf_size; ++Y) {
        for (int Z = 0; Z < pbi->bf_size; ++Z) {        
          /* lmax */
          for (int index_l1=0; index_l1 < pfi->l1_size; ++index_l1) {          
            for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
              free (pfi->fisher_matrix_XYZ_l1[X][Y][Z][index_l1][index_ft]);
              free (pfi->fisher_matrix_XYZ_lmax[X][Y][Z][index_l1][index_ft]);          
            }
            free (pfi->fisher_matrix_XYZ_l1[X][Y][Z][index_l1]);
            free (pfi->fisher_matrix_XYZ_lmax[X][Y][Z][index_l1]);
          }
          /* lmin */
          for (int index_l3=0; index_l3 < pfi->l1_size; ++index_l3) {          
            for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
              free (pfi->fisher_matrix_XYZ_l3[X][Y][Z][index_l3][index_ft]);
              free (pfi->fisher_matrix_XYZ_lmin[X][Y][Z][index_l3][index_ft]);          
            }
            free (pfi->fisher_matrix_XYZ_l3[X][Y][Z][index_l3]);
            free (pfi->fisher_matrix_XYZ_lmin[X][Y][Z][index_l3]);
          }
          /* common */
          free (pfi->fisher_matrix_XYZ_l1[X][Y][Z]);
          free (pfi->fisher_matrix_XYZ_lmax[X][Y][Z]);
          free (pfi->fisher_matrix_XYZ_l3[X][Y][Z]);
          free (pfi->fisher_matrix_XYZ_lmin[X][Y][Z]);
        }
        free (pfi->fisher_matrix_XYZ_l1[X][Y]);
        free (pfi->fisher_matrix_XYZ_lmax[X][Y]);
        free (pfi->fisher_matrix_XYZ_l3[X][Y]);
        free (pfi->fisher_matrix_XYZ_lmin[X][Y]);
      }
      free (pfi->fisher_matrix_XYZ_l1[X]);
      free (pfi->fisher_matrix_XYZ_lmax[X]);
      free (pfi->fisher_matrix_XYZ_l3[X]);
      free (pfi->fisher_matrix_XYZ_lmin[X]);
    }
    free (pfi->fisher_matrix_XYZ_l1);
    free (pfi->fisher_matrix_XYZ_lmax);
    free (pfi->fisher_matrix_XYZ_l3);
    free (pfi->fisher_matrix_XYZ_lmin);

    if (pfi->include_lensing_effects == _TRUE_) {
    
      for (int index_l3=0; index_l3 < pfi->l3_size; ++index_l3) {  
        for (int index_bf=0; index_bf < pbi->bf_size; ++index_bf) {
          for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
            free (pfi->fisher_matrix_CZ_l3[index_l3][index_ft*pbi->bf_size+index_bf]);    
          }
        }
        free (pfi->fisher_matrix_CZ_l3[index_l3]);
      }
      free (pfi->fisher_matrix_CZ_l3);
    }
    

    for(int index_l1=0; index_l1<pfi->l1_size; ++index_l1) {
    
      for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
        free (pfi->fisher_matrix_lmax[index_l1][index_ft]);
        free (pfi->fisher_matrix_l1[index_l1][index_ft]);
        free (pfi->inverse_fisher_matrix_lmax[index_l1][index_ft]);
      }
    
      free (pfi->fisher_matrix_lmax[index_l1]);
      free (pfi->fisher_matrix_l1[index_l1]);
      free (pfi->inverse_fisher_matrix_lmax[index_l1]);
      free (pfi->sigma_fnl_lmax[index_l1]);
    
    } // end of for(index_l1)
    
    free (pfi->fisher_matrix_lmax);
    free (pfi->fisher_matrix_l1);
    free (pfi->inverse_fisher_matrix_lmax);
    free (pfi->sigma_fnl_lmax);

    for(int index_l3=0; index_l3<pfi->l3_size; ++index_l3) {
    
      for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
        free (pfi->fisher_matrix_lmin[index_l3][index_ft]);
        free (pfi->fisher_matrix_l3[index_l3][index_ft]);
        free (pfi->inverse_fisher_matrix_lmin[index_l3][index_ft]);
      }
    
      free (pfi->fisher_matrix_lmin[index_l3]);
      free (pfi->fisher_matrix_l3[index_l3]);
      free (pfi->inverse_fisher_matrix_lmin[index_l3]);
      free (pfi->sigma_fnl_lmin[index_l3]);
    
    } // end of for(index_l3)

    free (pfi->fisher_matrix_lmin);
    free (pfi->fisher_matrix_l3);
    free (pfi->inverse_fisher_matrix_lmin);
    free (pfi->sigma_fnl_lmin);

    free (pfi->l1);
    free (pfi->l2);
    free (pfi->l3);
    

    /* Free 3j symbols */
    if ((pfi->bispectra_interpolation != mesh_interpolation)
       && (pfi->bispectra_interpolation != mesh_interpolation_2d)) 
      free (pfi->I_l1_l2_l3);


    /* Free meshes. Note that we start from the last bispectrum as the grid, which is shared between
    the various meshes, belongs to the index_ft=0 bispectrum, and hence should be freed last.  */
    if ((pfi->bispectra_interpolation == mesh_interpolation)
    || (pfi->bispectra_interpolation == mesh_interpolation_2d)
    && (pfi->has_only_analytical_bispectra == _FALSE_)) {
         
      for (int index_ft=(pfi->fisher_size-1); index_ft >= pfi->first_non_analytical_index_ft; --index_ft) {
        
        /* The analytical bispectra are never interpolated */
        if (pbi->bispectrum_type[pfi->index_bt_of_ft[index_ft]] == analytical_bispectrum)
          continue;
        
        for (int X = (pbi->bf_size-1); X >= 0; --X) {
          for (int Y = (pbi->bf_size-1); Y >= 0; --Y) {
            for (int Z = (pbi->bf_size-1); Z >= 0; --Z) {
              for (int index_mesh=0; index_mesh < pfi->n_meshes; ++index_mesh) {
     
                if ((index_mesh == 0) && (pfi->l_turnover[0] <= pbi->l[0]))
                  continue;
     
                if ((index_mesh == 1) && (pfi->l_turnover[0] > pbi->l[pbi->l_size-1]))
                  continue;
     
                class_call (mesh_free (pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]),
                  pfi->error_message,
                  pfi->error_message);      
              }
              free (pfi->mesh_workspaces[index_ft][X][Y][Z]);
            }
            free (pfi->mesh_workspaces[index_ft][X][Y]);
          }
          free (pfi->mesh_workspaces[index_ft][X]);
        }
        free (pfi->mesh_workspaces[index_ft]);
      }
      free (pfi->mesh_workspaces);
    }  // end of if(mesh)
    
    /* Free cross-power spectrum */
    for (int index_l=0; index_l < pbi->l_size; ++index_l) {
      for (int index_bf=0; index_bf < pbi->bf_size; ++index_bf) {
        free (pfi->C[index_l][index_bf]);
        free (pfi->inverse_C[index_l][index_bf]);
      }
      free (pfi->C[index_l]);
      free (pfi->inverse_C[index_l]);
    }
    free (pfi->C);
    free (pfi->inverse_C);

    /* Free noise spectrum */
    for (int index_bf=0; index_bf < pbi->bf_size; ++index_bf)
      free(pfi->N_l[index_bf]);
    free(pfi->N_l);

    
  } // end of if(has_fisher)
  
  return _SUCCESS_;
 
}




/**
 * This routine defines indices and allocates tables in the Fisher structure 
 *
 */

int fisher_indices (
        struct precision * ppr,
        struct background * pba,
        struct perturbs * ppt,
        struct bessels * pbs,
        struct transfers * ptr,
        struct primordial * ppm,
        struct spectra * psp,
        struct lensing * ple,
        struct bispectra * pbi,
        struct fisher * pfi
        )
{


  // ====================================================================================
  // =                              Which bispectra to use?                             =
  // ====================================================================================

  /* We won't need all the bispectra computed in the bispectra.c module in the Fisher
  matrix. Here we select those we are interested into, and create a correspondence
  between rows of the Fisher matrix and bispectra position in pbi->bispectra[index_bt] */

  int index_ft = 0;
  
  if (pbi->has_local_model) {
    pfi->index_ft_local = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_local;
    index_ft++;
  }
  if (pbi->has_equilateral_model) {
    pfi->index_ft_equilateral = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_equilateral;
    index_ft++;
  }  
  if (pbi->has_orthogonal_model) {
    pfi->index_ft_orthogonal = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_orthogonal;
    index_ft++;
  }
  if (pbi->has_galileon_model) {
    pfi->index_ft_galileon_gradient = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_galileon_gradient;
    index_ft++;
    
    pfi->index_ft_galileon_time = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_galileon_time;
    index_ft++;
  }
  if (pbi->has_local_squeezed == _TRUE_) {
    pfi->index_ft_local_squeezed = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_local_squeezed;
    index_ft++;
  }
  if (pbi->has_intrinsic_squeezed == _TRUE_) {
    pfi->index_ft_intrinsic_squeezed = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_intrinsic_squeezed;
    index_ft++;
  }
  if (pbi->has_cosine_shape == _TRUE_) {
    pfi->index_ft_cosine = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_cosine;
    index_ft++;
  }
  if (pbi->has_cmb_lensing == _TRUE_) {
    pfi->index_ft_cmb_lensing = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_cmb_lensing;
    index_ft++;
  }
  if (pbi->has_cmb_lensing_squeezed == _TRUE_) {
    pfi->index_ft_cmb_lensing_squeezed = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_cmb_lensing_squeezed;
    index_ft++;
  }
  if (pbi->has_cmb_lensing_kernel == _TRUE_) {
    pfi->index_ft_cmb_lensing_kernel = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_cmb_lensing_kernel;
    index_ft++;
  }
  if (pbi->has_intrinsic == _TRUE_) {
    pfi->index_ft_intrinsic = index_ft;
    pfi->index_bt_of_ft[index_ft] = pbi->index_bt_intrinsic;
    index_ft++;
  }
    
  pfi->fisher_size = index_ft;  

  if (pfi->fisher_verbose > 0) {
    printf (" -> Fisher matrix will contain %d row%s: ", pfi->fisher_size, (pfi->fisher_size!=1)?"s":"");
    for (int index_ft=0; index_ft < (pfi->fisher_size-1); ++index_ft)
      printf ("%s, ", pbi->bt_labels[pfi->index_bt_of_ft[index_ft]]);
    printf ("%s\n", pbi->bt_labels[pfi->index_bt_of_ft[pfi->fisher_size-1]]);
  }


  // ====================================================================================
  // =                                Determine l-sampling                              =
  // ====================================================================================

  /* Range in l where the bispectrum has been computed */
  pfi->l_min = pbi->l[0];
  pfi->l_max = pbi->l[pbi->l_size-1];
  pfi->full_l_size = pfi->l_max - pfi->l_min + 1;
  
  /* Which l-configurations should be included in the forecast? Modify by hand if you
  want to include specific configurations only, e.g. only squeezed ones. Remember that
  in the Fisher sum we consider only l1>=l2>=l3 */
  /* TODO: When choosing first l_max=1000 and then l_max=500, you get a slighly different
  result in the overlapping region. This happens with mesh_interpolation_2d but not with
  mesh_interpolation, suggesting an issue in the linear interpolation of l1. */
  pfi->l1_min_global = MAX (pfi->l_min_estimator, pfi->l_min);
  pfi->l2_min_global = MAX (pfi->l_min_estimator, pfi->l_min);
  pfi->l3_min_global = MAX (pfi->l_min_estimator, pfi->l_min);

  pfi->l1_max_global = MIN (pfi->l_max_estimator, pfi->l_max);
  pfi->l2_max_global = MIN (pfi->l_max_estimator, pfi->l_max);
  pfi->l3_max_global = MIN (pfi->l_max_estimator, pfi->l_max);

  /* Here we choose the 3D grid in (l1,l2,l3) over which to sum the estimator. For the mesh
  interpolation, where we interpolate the bispectra for all configurations, we need the full
  grid. For trilinear interpolation, where we sum over the support points, we only need 
  the l's where we computed the bispectra.  */

  if (pfi->bispectra_interpolation == mesh_interpolation) {

    pfi->l1_size = pfi->l2_size = pfi->l3_size = pfi->full_l_size;

    class_alloc (pfi->l1, pfi->full_l_size*sizeof(int), pfi->error_message);
    class_alloc (pfi->l2, pfi->full_l_size*sizeof(int), pfi->error_message);
    class_alloc (pfi->l3, pfi->full_l_size*sizeof(int), pfi->error_message);
    
    for (int index_l=0; index_l < pfi->full_l_size; ++index_l) {
      pfi->l1[index_l] = pfi->l_min + index_l;
      pfi->l2[index_l] = pfi->l_min + index_l;
      pfi->l3[index_l] = pfi->l_min + index_l;
    }    
  }
  /* mesh_2d means that we fully interpolate in the l2 and l3 level, but we only sum over
  the support points for the l1 level. */
  else if (pfi->bispectra_interpolation == mesh_interpolation_2d) {

    pfi->l1_size = pbi->l_size;
    pfi->l2_size = pfi->l3_size = pfi->full_l_size;

    class_alloc (pfi->l1, pbi->l_size*sizeof(int), pfi->error_message);
    class_alloc (pfi->l2, pfi->full_l_size*sizeof(int), pfi->error_message);
    class_alloc (pfi->l3, pfi->full_l_size*sizeof(int), pfi->error_message);
    
    for (int index_l=0; index_l < pbi->l_size; ++index_l) {
      pfi->l1[index_l] = pbi->l[index_l];
    }
    
    for (int index_l=0; index_l < pfi->full_l_size; ++index_l) {
      pfi->l2[index_l] = pfi->l_min + index_l;
      pfi->l3[index_l] = pfi->l_min + index_l;
    }
  }
  /* Any other type of interpolation sums over the node points only */
  else {

    pfi->l1_size = pfi->l2_size = pfi->l3_size = pbi->l_size;

    class_alloc (pfi->l1, pbi->l_size*sizeof(int), pfi->error_message);
    class_alloc (pfi->l2, pbi->l_size*sizeof(int), pfi->error_message);
    class_alloc (pfi->l3, pbi->l_size*sizeof(int), pfi->error_message);
    
    for (int index_l=0; index_l < pbi->l_size; ++index_l) {
      pfi->l1[index_l] = pbi->l[index_l];
      pfi->l2[index_l] = pbi->l[index_l];
      pfi->l3[index_l] = pbi->l[index_l];
    }
  }


  /* Allocate arrays for the noise in the C_l's */
  class_alloc (pfi->N_l, pbi->bf_size*sizeof(double *), pfi->error_message);
  for (int index_bf=0; index_bf < pbi->bf_size; ++index_bf)
    class_calloc (pfi->N_l[index_bf], pfi->full_l_size, sizeof(double), pfi->error_message);


  // =================================================================================================
  // =                               Allocate Fisher matrix arrays                                   =
  // =================================================================================================
  
  /* Allocate the arrays where we shall store the various contributions to the Fisher matrix. It is
  CRUCIAL that these arrays are initialised with calloc, as they will be incremented in the Fisher
  matrix estimator function.  */
  
  class_alloc (pfi->fisher_matrix_XYZ_l1, pbi->bf_size*sizeof(double *****), pfi->error_message);
  class_alloc (pfi->fisher_matrix_XYZ_lmax, pbi->bf_size*sizeof(double *****), pfi->error_message);
  class_alloc (pfi->fisher_matrix_XYZ_l3, pbi->bf_size*sizeof(double *****), pfi->error_message);
  class_alloc (pfi->fisher_matrix_XYZ_lmin, pbi->bf_size*sizeof(double *****), pfi->error_message);

  for (int X = 0; X < pbi->bf_size; ++X) {

    class_alloc (pfi->fisher_matrix_XYZ_l1[X], pbi->bf_size*sizeof(double ****), pfi->error_message);
    class_alloc (pfi->fisher_matrix_XYZ_lmax[X], pbi->bf_size*sizeof(double ****), pfi->error_message);
    class_alloc (pfi->fisher_matrix_XYZ_l3[X], pbi->bf_size*sizeof(double ****), pfi->error_message);
    class_alloc (pfi->fisher_matrix_XYZ_lmin[X], pbi->bf_size*sizeof(double ****), pfi->error_message);
        
    for (int Y = 0; Y < pbi->bf_size; ++Y) {

      class_alloc (pfi->fisher_matrix_XYZ_l1[X][Y], pbi->bf_size*sizeof(double ***), pfi->error_message);
      class_alloc (pfi->fisher_matrix_XYZ_lmax[X][Y], pbi->bf_size*sizeof(double ***), pfi->error_message);
      class_alloc (pfi->fisher_matrix_XYZ_l3[X][Y], pbi->bf_size*sizeof(double ***), pfi->error_message);
      class_alloc (pfi->fisher_matrix_XYZ_lmin[X][Y], pbi->bf_size*sizeof(double ***), pfi->error_message);

      for (int Z = 0; Z < pbi->bf_size; ++Z) {

        /* Arrays as a function of lmax (involves l1) */
        class_alloc (pfi->fisher_matrix_XYZ_l1[X][Y][Z], pfi->l1_size*sizeof(double **), pfi->error_message);
        class_alloc (pfi->fisher_matrix_XYZ_lmax[X][Y][Z], pfi->l1_size*sizeof(double **), pfi->error_message);

        for (int index_l1=0; index_l1 < pfi->l1_size; ++index_l1) {
          
          class_alloc (pfi->fisher_matrix_XYZ_l1[X][Y][Z][index_l1], pfi->fisher_size*sizeof(double *), pfi->error_message);
          class_alloc (pfi->fisher_matrix_XYZ_lmax[X][Y][Z][index_l1], pfi->fisher_size*sizeof(double *), pfi->error_message);
          
          for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
            class_calloc (pfi->fisher_matrix_XYZ_l1[X][Y][Z][index_l1][index_ft],
              pfi->fisher_size, sizeof(double), pfi->error_message);
            class_calloc (pfi->fisher_matrix_XYZ_lmax[X][Y][Z][index_l1][index_ft],
              pfi->fisher_size, sizeof(double), pfi->error_message);            
          }
        }

        /* Arrays as a function of lmin (involves l3) */
        class_alloc (pfi->fisher_matrix_XYZ_l3[X][Y][Z], pfi->l3_size*sizeof(double **), pfi->error_message);
        class_alloc (pfi->fisher_matrix_XYZ_lmin[X][Y][Z], pfi->l3_size*sizeof(double **), pfi->error_message);

        for (int index_l3=0; index_l3 < pfi->l3_size; ++index_l3) {
          
          class_alloc (pfi->fisher_matrix_XYZ_l3[X][Y][Z][index_l3], pfi->fisher_size*sizeof(double *), pfi->error_message);
          class_alloc (pfi->fisher_matrix_XYZ_lmin[X][Y][Z][index_l3], pfi->fisher_size*sizeof(double *), pfi->error_message);
          
          for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
            class_calloc (pfi->fisher_matrix_XYZ_l3[X][Y][Z][index_l3][index_ft],
              pfi->fisher_size, sizeof(double), pfi->error_message);
            class_calloc (pfi->fisher_matrix_XYZ_lmin[X][Y][Z][index_l3][index_ft],
              pfi->fisher_size, sizeof(double), pfi->error_message);
          }
        } 
        
      } // Z
    } // Y 
  } // X
  
  /* More arrays as a function of lmax (involving l1) */
  class_alloc (pfi->fisher_matrix_lmax, pfi->l1_size*sizeof(double **), pfi->error_message);
  class_alloc (pfi->fisher_matrix_l1, pfi->l1_size*sizeof(double **), pfi->error_message);
  class_alloc (pfi->inverse_fisher_matrix_lmax, pfi->l1_size*sizeof(double **), pfi->error_message);
  class_alloc (pfi->sigma_fnl_lmax, pfi->l1_size*sizeof(double *), pfi->error_message);
  
  for (int index_l1=0; index_l1 < pfi->l1_size; ++index_l1) {

    class_alloc (pfi->fisher_matrix_lmax[index_l1], pfi->fisher_size*sizeof(double *), pfi->error_message);
    class_alloc (pfi->fisher_matrix_l1[index_l1], pfi->fisher_size*sizeof(double *), pfi->error_message);
    class_alloc (pfi->inverse_fisher_matrix_lmax[index_l1], pfi->fisher_size*sizeof(double *), pfi->error_message);
    class_calloc (pfi->sigma_fnl_lmax[index_l1], pfi->fisher_size, sizeof(double), pfi->error_message);
    
    for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
      class_calloc (pfi->fisher_matrix_lmax[index_l1][index_ft], pfi->fisher_size, sizeof(double), pfi->error_message);
      class_calloc (pfi->fisher_matrix_l1[index_l1][index_ft], pfi->fisher_size, sizeof(double), pfi->error_message);
      class_calloc (pfi->inverse_fisher_matrix_lmax[index_l1][index_ft], pfi->fisher_size, sizeof(double), pfi->error_message);
    }    
  }

  /* More arrays as a function of lmin (involving l3) */
  class_alloc (pfi->fisher_matrix_lmin, pfi->l3_size*sizeof(double **), pfi->error_message);
  class_alloc (pfi->fisher_matrix_l3, pfi->l3_size*sizeof(double **), pfi->error_message);
  class_alloc (pfi->inverse_fisher_matrix_lmin, pfi->l3_size*sizeof(double **), pfi->error_message);
  class_alloc (pfi->sigma_fnl_lmin, pfi->l3_size*sizeof(double *), pfi->error_message);
  
  for (int index_l3=0; index_l3 < pfi->l3_size; ++index_l3) {

    class_alloc (pfi->fisher_matrix_lmin[index_l3], pfi->fisher_size*sizeof(double *), pfi->error_message);
    class_alloc (pfi->fisher_matrix_l3[index_l3], pfi->fisher_size*sizeof(double *), pfi->error_message);
    class_alloc (pfi->inverse_fisher_matrix_lmin[index_l3], pfi->fisher_size*sizeof(double *), pfi->error_message);
    class_calloc (pfi->sigma_fnl_lmin[index_l3], pfi->fisher_size, sizeof(double), pfi->error_message);
    
    for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
      class_calloc (pfi->fisher_matrix_lmin[index_l3][index_ft], pfi->fisher_size, sizeof(double), pfi->error_message);
      class_calloc (pfi->fisher_matrix_l3[index_l3][index_ft], pfi->fisher_size, sizeof(double), pfi->error_message);
      class_calloc (pfi->inverse_fisher_matrix_lmin[index_l3][index_ft], pfi->fisher_size, sizeof(double), pfi->error_message);
    }    
  }

  if (pfi->include_lensing_effects == _TRUE_) {

    /* Allocate fisher_matrix_CZ_l3, needed to compute the lensing variance. It is equivalent to \bar{F}_{l_1}^{ip}
    in Eq. 5.25 of Lewis et al. 2011 (see header file). */
    class_alloc (pfi->fisher_matrix_CZ_l3, pfi->l3_size*sizeof(double **), pfi->error_message);
    for (int index_l3=0; index_l3 < pfi->l3_size; ++index_l3) {
      class_alloc (pfi->fisher_matrix_CZ_l3[index_l3], pbi->bf_size*pfi->fisher_size*sizeof(double *), pfi->error_message);
      for (int index_bf=0; index_bf < pbi->bf_size; ++index_bf) {
        for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
          class_calloc (pfi->fisher_matrix_CZ_l3[index_l3][index_ft*pbi->bf_size+index_bf],
          pbi->bf_size*pfi->fisher_size, sizeof(double), pfi->error_message);
        }
      }
    }
    
    /* Arrays that will contain the Fisher matrix including lensing variance */
    class_alloc (pfi->fisher_matrix_lensvar_lmin, pfi->l3_size*sizeof(double **), pfi->error_message);
    class_alloc (pfi->fisher_matrix_lensvar_l3, pfi->l3_size*sizeof(double **), pfi->error_message);
    class_alloc (pfi->inverse_fisher_matrix_lensvar_lmin, pfi->l3_size*sizeof(double **), pfi->error_message);
    class_alloc (pfi->sigma_fnl_lensvar_lmin, pfi->l3_size*sizeof(double *), pfi->error_message);
  
    for (int index_l3=0; index_l3 < pfi->l3_size; ++index_l3) {

      class_alloc (pfi->fisher_matrix_lensvar_lmin[index_l3], pfi->fisher_size*sizeof(double *), pfi->error_message);
      class_alloc (pfi->fisher_matrix_lensvar_l3[index_l3], pfi->fisher_size*sizeof(double *), pfi->error_message);
      class_alloc (pfi->inverse_fisher_matrix_lensvar_lmin[index_l3], pfi->fisher_size*sizeof(double *), pfi->error_message);
      class_calloc (pfi->sigma_fnl_lensvar_lmin[index_l3], pfi->fisher_size, sizeof(double), pfi->error_message);
    
      for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
        class_calloc (pfi->fisher_matrix_lensvar_lmin[index_l3][index_ft], pfi->fisher_size, sizeof(double), pfi->error_message);
        class_calloc (pfi->fisher_matrix_lensvar_l3[index_l3][index_ft], pfi->fisher_size, sizeof(double), pfi->error_message);
        class_calloc (pfi->inverse_fisher_matrix_lensvar_lmin[index_l3][index_ft], pfi->fisher_size, sizeof(double), pfi->error_message);
      }    
    }
  } // end of if(include_lensing_effects)


  // =========================================================================================
  // =                                 Interpolation arrays                                  = 
  // =========================================================================================

  /* Determine which is the first bispectrum for which we should compute the mesh. This is
  equivalent to the first bispectrum that is not of the analytical type, as these are not
  interpolated at all */
  pfi->first_non_analytical_index_ft = 0;
  while (pbi->bispectrum_type[pfi->index_bt_of_ft[pfi->first_non_analytical_index_ft]] == analytical_bispectrum
  && (pfi->first_non_analytical_index_ft<pfi->fisher_size))
    pfi->first_non_analytical_index_ft++;
  
  /* No need to compute meshes if all bispectra are analytical, as interpolation is not
  needed in this case */
  pfi->has_only_analytical_bispectra = _FALSE_;
  if (pfi->first_non_analytical_index_ft == pfi->fisher_size)
    pfi->has_only_analytical_bispectra = _TRUE_;

  if ((pfi->bispectra_interpolation == mesh_interpolation)
  || (pfi->bispectra_interpolation == mesh_interpolation_2d)
  && (pfi->has_only_analytical_bispectra == _FALSE_)) {
  
    /* TODO: generalize this, or modify the mesh_sort/mesh_int functions */
    pfi->n_meshes = 2;
    
    /* Allocate arrays that will contain the parameters for the mesh interpolation */
    class_alloc (pfi->link_lengths, pfi->n_meshes*sizeof(double), pfi->error_message);
    class_alloc (pfi->group_lengths, pfi->n_meshes*sizeof(double), pfi->error_message);
    class_alloc (pfi->soft_coeffs, pfi->n_meshes*sizeof(double), pfi->error_message);
  
    /* Turnover point between the two meshes */
    class_alloc (pfi->l_turnover, (pfi->n_meshes-1)*sizeof(int), pfi->error_message);

    /* Allocate one mesh interpolation workspace per type of bispectrum, skipping the
    analytical bispectra because they don't need to be interpolated */
    class_alloc (pfi->mesh_workspaces,
      pfi->fisher_size*sizeof(struct mesh_interpolation_workspace *****),
      pfi->error_message);
  
    /* Allocate worspaces and intialize counters */
    for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
      
      /* The analytical bispectra are never interpolated */
      if (pbi->bispectrum_type[pfi->index_bt_of_ft[index_ft]] == analytical_bispectrum)
        continue;

      class_alloc (pfi->mesh_workspaces[index_ft],
        pbi->bf_size*sizeof(struct mesh_interpolation_workspace ****),
        pfi->error_message);

        for (int X = 0; X < pbi->bf_size; ++X) {

          class_alloc (pfi->mesh_workspaces[index_ft][X],
            pbi->bf_size*sizeof(struct mesh_interpolation_workspace ***),
            pfi->error_message);

          for (int Y = 0; Y < pbi->bf_size; ++Y) {
            
            class_alloc (pfi->mesh_workspaces[index_ft][X][Y],
              pbi->bf_size*sizeof(struct mesh_interpolation_workspace **),
              pfi->error_message);

            for (int Z = 0; Z < pbi->bf_size; ++Z) {

              class_alloc (pfi->mesh_workspaces[index_ft][X][Y][Z],
                pfi->n_meshes*sizeof(struct mesh_interpolation_workspace *),
                pfi->error_message);
      
              for (int index_mesh=0; index_mesh < pfi->n_meshes; ++index_mesh) {
        
                class_alloc (pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh],
                  sizeof(struct mesh_interpolation_workspace),
                  pfi->error_message);

                pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->n_allocated_in_grid = 0;
                pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->n_allocated_in_mesh = 0; 
                pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->count_interpolations = 0;
                pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->count_range_extensions = 0;
        
            } // end of for(index_mesh)
          } // end of for(i)
        } // end of for(j)
      } // end of for(k)
    } // end of for(index_ft)
  } // end of if(mesh_interpolation)
  


  // =================================================================================================
  // =                                    Compute three_j symbol                                     =
  // =================================================================================================
  
  /* In the following lines, we shall allocate and fill the (l1,l2,l3) array containing the quantity
    I_l1_l2_l3 in eq. 13 of Komatsu, Spergel & Wandelt (2005):
  
     I_l1_l2_l3 = sqrt( (2L1+1)(2L2+1)(2L3+1)/4*pi ) *  (L1 L2 L3)
                                                        (0  0  0 )
  
    which is needed to compute the angle-averaged bispectra, which in turn are used by the f_NL
    estimator. */
  
  /* For mesh interpolation, we shall compute the 3j's directly in the estimator */
  if ((pfi->bispectra_interpolation != mesh_interpolation) 
   && (pfi->bispectra_interpolation != mesh_interpolation_2d)) {
  
    if (pfi->fisher_verbose > 1)
      printf(" -> allocating and computing ~ %.3g MB (%ld doubles) of 3j-symbols\n",
        pbi->n_independent_configurations*sizeof(double)/1e6, pbi->n_independent_configurations);
  
    /* Allocate memory for pfi->I_l1_l2_l3 */
    class_alloc (pfi->I_l1_l2_l3, pbi->n_independent_configurations*sizeof(double), pfi->error_message);
  
    /* Temporary values needed for the computation of the 3j symbol. The temporary array will contain at most 2*l_max+1
      values when l1=l2=l_max, with the +1 accounting for the l=0 case. */
    double * temporary_three_j;
    class_alloc (temporary_three_j, (2*pfi->l_max+1)*sizeof(double), pfi->error_message);
    double l3_min_D, l3_max_D;
             
    /* Fill the I_l1_l2_l3 array */
    /* TODO:   parallelize the loop by creating a 3j array per thread */
    // #pragma omp parallel \
    // private (index_l1, index_l2, index_l3, l3_min, l3_max, temporary_three_j_size, index_l1_l2_l3)
    
    for (int index_l1=0; index_l1 < pbi->l_size; ++index_l1) {
      
      int l1 = pbi->l[index_l1];
    
      for (int index_l2=0; index_l2 <= index_l1; ++index_l2) {
        
        int l2 = pbi->l[index_l2];
        
        /* Skip those configurations that are forbidden by the triangular condition (optimization) */
        if (l2 < l1/2)
          continue;
        
        int index_l3_min = pbi->index_l_triangular_min[index_l1][index_l2];
        int index_l3_max = MIN (index_l2, pbi->index_l_triangular_max[index_l1][index_l2]);

        /* Compute the actual 3j symbol for all allowed values of l3 */
        class_call (drc3jj (
                      l1, l2, 0, 0,
                      &l3_min_D, &l3_max_D,
                      temporary_three_j,
                      (2*pfi->l_max+1),
                      pfi->error_message       
                      ),
          pfi->error_message,
          pfi->error_message);

        int l3_min = (int)(l3_min_D+_EPS_);
    
        /* Fill the 3j array */
        for (int index_l3=index_l3_min; index_l3<=index_l3_max; ++index_l3) {
                
          int l3 = pbi->l[index_l3];
          
          /* Index of the current (l1,l2,l3) configuration */
          long int index_l1_l2_l3 = pbi->index_l1_l2_l3[index_l1][index_l1-index_l2][index_l3_max-index_l3];
    
          /* This 3j symbol must vanish when l1+l2+l3 is even. */
          class_test (((l1+l2+l3)%2!=0) && (temporary_three_j[l3-l3_min] != 0.),
            pfi->error_message,
            "error in computation of 3j, odd is not zero!");
        
          /* The following is what it takes to convert a reduced bispectrum to an angle-averaged one */
          pfi->I_l1_l2_l3[index_l1_l2_l3] =
            sqrt((2.*l1+1.)*(2.*l2+1.)*(2.*l3+1.)/(4.*_PI_)) * temporary_three_j[l3-l3_min];
  
          /* Some debug */
          // fprintf (stderr, "%5d %5d %5d %5d/%5d %17.7g %17.7g %17.7g\n",
          //   l1, l2, l3, index_l1_l2_l3, pbi->n_independent_configurations-1,
          //     pfi->I_l1_l2_l3[index_l1_l2_l3], sqrt((2.*l1+1.)*(2.*l2+1.)*(2.*l3+1.)/(4.*_PI_)), temporary_three_j[l3-l3_min]);
  
        } // end of for(index_l3)
      } // end of for(index_l2)
    } // end of for(index_l1)
  
    free (temporary_three_j);
  
  } // end of if not mesh interpolation

  /* Some debug - Double-check the 3j routine */
  // int l1=1200;
  // int l2=1500;
  // 
  // double * temporary_three_j;
  // class_alloc (temporary_three_j, (l1+l2+1)*sizeof(double), pfi->error_message);
  // double l3_min_D, l3_max_D, temporary_three_j_size;
  // 
  // class_call (drc3jj (
  //               l1, l2, 0, 0,
  //               &l3_min_D, &l3_max_D,
  //               temporary_three_j,
  //               (2*pfi->l_max+1),
  //               pfi->error_message       
  //               ),
  //   pfi->error_message,
  //   pfi->error_message);
  //   
  // int l3_min = (int)(l3_min_D+_EPS_);
  // int l3_max = (int)(l3_max_D+_EPS_);
  //  
  // for (int l3=l3_min; l3<=l3_max; ++l3)
  //   printf ("I(%d,%d,%d)=%.15g\n", l1,l2,l3,temporary_three_j[l3-l3_min]);

  // ==================================================================================================
  // =                                      Compute wasted points                                     =
  // ==================================================================================================

  /* Count the number of bispectra configurations that won't be used for the interpolation */
  long int count_wasted = 0;

  for (int index_l1=0; index_l1 < pbi->l_size; ++index_l1) {
    int l1 = pbi->l[index_l1];
    for (int index_l2=0; index_l2 <= index_l1; ++index_l2) {
      int l2 = pbi->l[index_l2];
      int index_l3_min = pbi->index_l_triangular_min[index_l1][index_l2];
      int index_l3_max = MIN (index_l2, pbi->index_l_triangular_max[index_l1][index_l2]);
      for (int index_l3=index_l3_min; index_l3<=index_l3_max; ++index_l3)
        if ((l1+l2+pbi->l[index_l3])%2!=0)
          count_wasted++;
    }
  }

  if (pfi->fisher_verbose > 1)
    printf ("     * %.3g%% of the computed bispectra configurations will be (directly) used to compute the Fisher matrix (%ld wasted)\n",
    100-count_wasted/(double)(pbi->n_independent_configurations)*100, count_wasted);
  
  return _SUCCESS_;

}



/**
 * Fill the pfi->N_l arrays with the observational noise for the C_l's.
 *
 * For the time being, we have implemented the noise model in astro-ph/0506396v2, a co-added
 * Gaussian noise where the beam for each frequency channel of the experiment is considered
 * to be Gaussian, and the noise homogeneous.
 *
 */
int fisher_noise (
        struct precision * ppr,
        struct background * pba,
        struct perturbs * ppt,
        struct bessels * pbs,
        struct transfers * ptr,
        struct primordial * ppm,
        struct spectra * psp,
        struct lensing * ple,
        struct bispectra * pbi,
        struct fisher * pfi
        )
{
  
  // ===================================================================================
  // =                       Read in the noise values for the fields                   =
  // ===================================================================================

  double noise[pbi->bf_size][pfi->n_channels];
  
  for (int index_bf=0; index_bf < pbi->bf_size; ++index_bf) {
    for (int index_channel=0; index_channel < pfi->n_channels; ++index_channel) {

      if ((pbi->has_bispectra_t == _TRUE_) && (index_bf == pbi->index_bf_t))
        noise[index_bf][index_channel] = pfi->noise_t[index_channel];

      if ((pbi->has_bispectra_e == _TRUE_) && (index_bf == pbi->index_bf_e))
        noise[index_bf][index_channel] = pfi->noise_e[index_channel];

      if ((pbi->has_bispectra_r == _TRUE_) && (index_bf == pbi->index_bf_r))
        noise[index_bf][index_channel] = pfi->noise_r[index_channel];
    }
  }


  // ===================================================================================
  // =                                Print information                                =
  // ===================================================================================

  if (pfi->fisher_verbose > 0) {

    printf (" -> noise model: beam_fwhm=");
    for (int index_channel=0; index_channel < pfi->n_channels-1; ++index_channel)
      printf ("%g,", pfi->beam[index_channel] * 60 * 180./_PI_);
    printf ("%g", pfi->beam[pfi->n_channels-1] * 60 * 180./_PI_);
    printf (" (arcmin)");
    
    for (int index_bf=0; index_bf < pbi->bf_size; ++index_bf) {

      printf (", sigma_%s=", pbi->bf_labels[index_bf]);
      for (int index_channel=0; index_channel < pfi->n_channels-1; ++index_channel)
        printf ("%g,", sqrt(noise[index_bf][index_channel]) * 1e6*pba->T_cmb / pfi->beam[index_channel]);
      printf ("%g", sqrt(noise[index_bf][pfi->n_channels-1]) * 1e6*pba->T_cmb / pfi->beam[pfi->n_channels-1]);
      printf (" (uK)");
    }
    printf ("\n");
  }



  // ===================================================================================
  // =                             Co-added Gaussian noise                             =
  // ===================================================================================
  
  /* Eeach frequency channel contributes to the inverse noise with a 1/N contribution */
  for (int index_bf=0; index_bf < pbi->bf_size; ++index_bf) {
    for (int l=pfi->l_min; l <= pfi->l_max; ++l) {
      for (int index_channel=0; index_channel < pfi->n_channels; ++index_channel) {

        double beam_exponent = (l*(l+1.)*pfi->beam[index_channel]*pfi->beam[index_channel])/(8.*log(2.));

        class_test (beam_exponent > DBL_MAX_EXP,
          pfi->error_message,
          "stop to prevent overflow. Reduce experiment_beam_fwhm to something more reasonable, or set it to zero.")
        
        /* Inverse contribution to l from the considered channel */
        pfi->N_l[index_bf][l-2] += 1. / (noise[index_bf][index_channel] * exp(beam_exponent));
      }

      /* Invert back */
      pfi->N_l[index_bf][l-2] = 1. / pfi->N_l[index_bf][l-2];

      /* Is this really needed? If the C_l's are infinite, it means that the signal is
      swamped by the noise, which is not an error */
      class_test (fabs(pfi->N_l[index_bf][l-2]) > _HUGE_,
        pfi->error_message,
        "stopping to avoid infinities, reduce noise parameters: pfi->N_%d(%s) = %g",
        l, pbi->bf_labels[index_bf], pfi->N_l[index_bf][l-2]);
    
      /* Some debug */
      // double factor = 1e12*pba->T_cmb*pba->T_cmb*l*(l+1)/(2*_PI_);
      // fprintf (stderr, "%d %g %g\n", l, pfi->N_l[pbi->index_bf_t][l-2], factor*pfi->N_l[pbi->index_bf_t][l-2]);
    
    } // end of for(l=2)
  } // end of for(T,E,...)
  

  return _SUCCESS_;
  
}



/**
 * Build the mesh for the interpolation of the bispectra.
 *
 */
int fisher_create_interpolation_mesh(
        struct precision * ppr,
        struct background * pba,
        struct perturbs * ppt,
        struct bessels * pbs,
        struct transfers * ptr,
        struct primordial * ppm,
        struct spectra * psp,
        struct lensing * ple,
        struct bispectra * pbi,
        struct fisher * pfi
        )
{
  
  if (pfi->fisher_verbose > 0)
    printf (" -> preparing the interpolation mesh for %d bispectr%s\n",
    pfi->fisher_size*pbi->n_probes, ((pfi->fisher_size*pbi->n_probes)!=1?"a":"um"));

  // ==================================================================================
  // =                           Interpolation parameters                             =
  // ==================================================================================
  
  // Fine mesh parameters
        
  pfi->link_lengths[0] = 2 * (pbi->l[1] - pbi->l[0]);
  pfi->group_lengths[0] = 0.1 * (pbi->l[1] - pbi->l[0]);
  pfi->soft_coeffs[0] = 0.5;  


  // Coarse mesh parameters

  /* We will choose the linking length based on ppr->l_linstep, which determines the
  (linear) step between the l's for high values of l_linstep. */
  int l_linstep = ppr->l_linstep;

  /* If ppr->l_linstep=1, all l's are used and there is effectively no interpolation.
  However, if also ppr->compute_only_even_ls == _TRUE_, then the actual step between one
  multipole and the other is doubled, as the odd l's are skipped. */
  if ((l_linstep==1) && (ppr->compute_only_even_ls==_TRUE_))
    l_linstep = 2;

  pfi->link_lengths[1] = 0.5/sqrt(2) * l_linstep;
  pfi->group_lengths[1] = 0.1 * l_linstep;
  pfi->soft_coeffs[1] = 0.5;
  
  for (int index_mesh=0; index_mesh < pfi->n_meshes; ++index_mesh)
    class_test (pfi->link_lengths[index_mesh] <= pfi->group_lengths[index_mesh],
      pfi->error_message,
      "the linking length must be larger than the grouping length.")
  

  // ==================================================================================
  // =                        Determine the turnover points                           =
  // ==================================================================================
  
  /* Never use the fine grid if the large step is used since the beginning */
  if ((pbi->l[1]-pbi->l[0]) >= l_linstep) {
    pfi->l_turnover[0] = pbi->l[0];
  }
  /* Never use the coarse grid if the linstep (i.e. the largest possible step) is never used.
  The MAX() is needed because the last point is fixed and does not depend on the grid spacing. */
  else if (MAX(pbi->l[pbi->l_size-1]-pbi->l[pbi->l_size-2], pbi->l[pbi->l_size-2]-pbi->l[pbi->l_size-3]) < l_linstep) {
    pfi->l_turnover[0] = pbi->l[pbi->l_size-1] + 1;
  }
  else {
    int index_l = 0;
    while ((index_l < pbi->l_size-1) && ((pbi->l[index_l+1] - pbi->l[index_l]) < l_linstep))
      index_l++;
    pfi->l_turnover[0] = pbi->l[index_l-1];
  }


  if (pfi->fisher_verbose > 1)
    printf ("     * l_turnover=%d, n_boxes=[%d,%d], linking lengths=[%g,%g], grouping lengths=[%g,%g]\n",
      pfi->l_turnover[0],
      (int)ceil(pfi->l_turnover[0]/ (pfi->link_lengths[0]*(1.+pfi->soft_coeffs[0]))),
      (int)ceil(pbi->l[pbi->l_size-1] / (pfi->link_lengths[1]*(1.+pfi->soft_coeffs[1]))),
      pfi->link_lengths[0], pfi->link_lengths[1],
      pfi->group_lengths[0], pfi->group_lengths[1]);
    

  // ==================================================================================
  // =                                Create meshes                                   =
  // ==================================================================================

  if (pfi->fisher_verbose > 1)
    printf ("     * allocating memory for values...\n");

  /* Allocate array that will contain the values of the bispectra arranged in the right order for mesh sorting */
  double ** values;
  class_alloc (values, pbi->n_independent_configurations*sizeof(double *), pfi->error_message);

  for (long int index_l1_l2_l3=0; index_l1_l2_l3 < pbi->n_independent_configurations; ++index_l1_l2_l3)
    class_alloc (values[index_l1_l2_l3], 4*sizeof(double), pfi->error_message);

  /* Determine which C_l's to use for the window function (see longer comment below, in innermost loop) */
  if (pbi->has_bispectra_t == _TRUE_)
    pfi->index_ct_window = psp->index_ct_tt;
  else if (pbi->bf_size == 1)
    pfi->index_ct_window = pbi->index_ct_of_bf_bf[0][0];
  else
    class_stop (pfi->error_message, "bispectra with two non-temperature fields are not implemented yet.\n");

  if (pfi->fisher_verbose > 1)
    printf ("     * computing actual meshes...\n");  
  
  // ---------------------------------------------------------------------------
  // -                          Rearrange the bispectra                        -
  // ---------------------------------------------------------------------------

  int abort = _FALSE_;

  #pragma omp parallel for schedule (dynamic)
  for (int index_l1 = 0; index_l1 < pbi->l_size; ++index_l1) {

    int l1 = pbi->l[index_l1];
    
    /* Arrays that will contain the 3j symbols for a given (l1,l2) */
    double threej_000[2*pbi->l_max+1], threej_m220[2*pbi->l_max+1],
           threej_0m22[2*pbi->l_max+1], threej_20m2[2*pbi->l_max+1];

    /* First l-multipole stored in the above arrays */
    int l3_min_000=0, l3_min_0m22=0, l3_min_20m2=0, l3_min_m220=0;

    for (int index_l2 = 0; index_l2 <= index_l1; ++index_l2) {
  
      int l2 = pbi->l[index_l2];
      
      /* Determine the limits for l3, which come from the triangular inequality |l1-l2| <= l3 <= l1+l2 */
      int index_l3_min = pbi->index_l_triangular_min[index_l1][index_l2];
      int index_l3_max = MIN (index_l2, pbi->index_l_triangular_max[index_l1][index_l2]);

      /* Compute 3j-symbols to compute the window function */
      if (pbi->need_3j_symbols == _TRUE_) {

        double min_D, max_D;

        class_call_parallel (drc3jj (
                               l1, l2, 0, 0,
                               &min_D, &max_D,
                               threej_000,
                               (2*pbi->l_max+1),
                               pfi->error_message),
          pfi->error_message,
          pfi->error_message);
        l3_min_000 = (int)(min_D + _EPS_);
    
        class_call_parallel (drc3jj (
                               l1, l2, 0, -2,
                               &min_D, &max_D,
                               threej_0m22,
                               (2*pbi->l_max+1),
                               pfi->error_message),
          pfi->error_message,
          pfi->error_message);
        l3_min_0m22 = (int)(min_D + _EPS_);

        class_call_parallel (drc3jj (
                               l1, l2, 2, 0,
                               &min_D, &max_D,
                               threej_20m2,
                               (2*pbi->l_max+1),
                               pfi->error_message),
          pfi->error_message,
          pfi->error_message);
        l3_min_20m2 = (int)(min_D + _EPS_);

        class_call_parallel (drc3jj (
                               l1, l2, -2, 2,
                               &min_D, &max_D,
                               threej_m220,
                               (2*pbi->l_max+1),
                               pfi->error_message),
          pfi->error_message,
          pfi->error_message);
        l3_min_m220 = (int)(min_D + _EPS_);

      } // end of 3j computation

      for (int index_l3=index_l3_min; index_l3<=index_l3_max; ++index_l3) {

        int l3 = pbi->l[index_l3];
        long int index_l1_l2_l3 = pbi->index_l1_l2_l3[index_l1][index_l1-index_l2][index_l3_max-index_l3];
          
        for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {

          int index_bt = pfi->index_bt_of_ft[index_ft];
    
          /* The analytical bispectra are never interpolated */
          if (pbi->bispectrum_type[index_bt] == analytical_bispectrum)
            continue;
          
          for (int X = 0; X < pbi->bf_size; ++X) {
            for (int Y = 0; Y < pbi->bf_size; ++Y) {
              for (int Z = 0; Z < pbi->bf_size; ++Z) {

                /* By default, assume that no window function is needed */
                double window = 1;
                
                /* Compute the value of the window function for this (l1,l2,l3) */
                if (pbi->window_function[index_bt] != NULL)
                  class_call_parallel ((*pbi->window_function[index_bt]) (
                                ppr, psp, ple, pbi,
                                l1, l2, l3,
                                X, Y, Z,
                                threej_000[l3-l3_min_000],
                                threej_20m2[l3-l3_min_20m2],
                                threej_m220[l3-l3_min_m220],
                                threej_0m22[l3-l3_min_0m22],
                                &window),
                    pbi->error_message,
                    pbi->error_message);

                /* The 0th element of 'values' is the function to be interpolated. The natural scaling for the bispectrum
                (both the templates and the second-order one) is given by Cl1*Cl2 + Cl2*Cl3 + Cl1*Cl3, which we adopt
                as a window function. When available, we always use the C_l's for the temperature, as they are approximately
                the same order of magnitude for all l's, contrary to those for polarization which are very small for l<200. */
                values[index_l1_l2_l3][0] = window * pbi->bispectra[index_bt][X][Y][Z][index_l1_l2_l3];

                /* 1st to 3rd arguments are the coordinates */
                values[index_l1_l2_l3][1] = (double)(l1);
                values[index_l1_l2_l3][2] = (double)(l2);
                values[index_l1_l2_l3][3] = (double)(l3);
                
              } // end of for(Z)
            } // end of for(Y)
          } // end of for(X)
        }  // end of for(index_ft)
      } // end of for(index_l3)
    } // end of for(index_l2)
  } // end of for(index_l1)
  if (abort == _TRUE_) return _FAILURE_; // end of parallel region


  // ---------------------------------------------------------------------------
  // -                            Generate the meshes                          -
  // ---------------------------------------------------------------------------

  for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {

    int index_bt = pfi->index_bt_of_ft[index_ft];
    
    /* The analytical bispectra are never interpolated */
    if (pbi->bispectrum_type[index_bt] == analytical_bispectrum)
      continue;

    for (int X = 0; X < pbi->bf_size; ++X) {
      for (int Y = 0; Y < pbi->bf_size; ++Y) {
        for (int Z = 0; Z < pbi->bf_size; ++Z) {

          for (int index_mesh=0; index_mesh < pfi->n_meshes; ++index_mesh) {
  
            /* Set the maximum l. The fine grid does not need to go up to l_max */
            if (index_mesh == 0)
              pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->l_max = (double)pfi->l_turnover[0];
            else 
              pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->l_max = (double)pbi->l[pbi->l_size-1];

            pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->n_points = pbi->n_independent_configurations;
            pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->link_length = pfi->link_lengths[index_mesh];
            pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->group_length = pfi->group_lengths[index_mesh];
            pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->soft_coeff = pfi->soft_coeffs[index_mesh];

            /* Since the grid is shared between different bispectra, it needs to be computed only for the first one.
            The first one is not necessarily index_ft=0 because analytical bispectra don't need to be interpolated
            at all */
            if ((index_ft == pfi->first_non_analytical_index_ft) && (X == 0) && (Y == 0) && (Z == 0)) {
              pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->compute_grid = _TRUE_;
            }
            else {
              pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->compute_grid = _FALSE_;
              pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->grid =
                pfi->mesh_workspaces[pfi->first_non_analytical_index_ft][0][0][0][index_mesh]->grid;
            }
  
            if (pfi->fisher_verbose > 2) 
              printf ("     * computing mesh for bispectrum %s_%s(%d)\n",
              pbi->bt_labels[index_bt], pbi->bfff_labels[X][Y][Z], index_mesh);
              
            /* Skip the fine grid if the turnover point is smaller than than the smallest multipole */
            if ((index_mesh == 0) && (pfi->l_turnover[0] <= pbi->l[0])) {
              if (pfi->fisher_verbose > 2)
                printf ("      \\ fine grid not needed because l_turnover <= l_min (%d <= %d)\n", pfi->l_turnover[0], pbi->l[0]);
              continue;
            }
  
            /* Skip the coarse grid if the turnover point is larger than than the largest multipole */
            if ((index_mesh == 1) && (pfi->l_turnover[0] > pbi->l[pbi->l_size-1])) {
              if (pfi->fisher_verbose > 2)
                printf ("      \\ coarse grid not needed because l_turnover > l_max (%d > %d)\n", pfi->l_turnover[0],
                pbi->l[pbi->l_size-1]);
              continue;
            }
            
            /* Generate the mesh. This function is already parallelised. */
            class_call (mesh_sort (
                          pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh],
                          values),
              pfi->error_message,
              pfi->error_message);
  
            if (pfi->fisher_verbose > 2)
              printf ("      \\ allocated (grid,mesh)=(%g,%g) MBs\n",
                pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->n_allocated_in_grid*8/1e6,
                pfi->mesh_workspaces[index_ft][X][Y][Z][index_mesh]->n_allocated_in_mesh*8/1e6);
  
          } // end of for(index_mesh)
        } // end of for(Z)
      } // end of for(Y)
    } // end of for(X)
  }  // end of for(index_ft)
  
  /* We do not need the node values anymore */
  for (long int index_l1_l2_l3=0; index_l1_l2_l3 < pbi->n_independent_configurations; ++index_l1_l2_l3)
    free (values[index_l1_l2_l3]);
  
  free (values);
  
  /* Check whether we recover the node point b(2,2,2) */
  // double interpolated_bispectrum;
  // 
  // class_call (mesh_int(pbi, 0, 2., 2., 2., &interpolated_bispectrum), pfi->error_message, pfi->error_message);
  // 
  // 
  // 
  // printf ("%g %g\n", pbi->bispectrum_mesh[0][0][0][0][0][0], interpolated_bispectrum);
  
  
  /* Print a slice of interpolated bispectrum */
  // int index_l1_debug = 50;
  // int index_l2_debug = 50;
  // int index_l3;
  // 
  // int index_l_triangular_min = pbi->index_l_triangular_min[index_l1_debug][index_l2_debug];
  // int l_triangular_size = pbi->l_triangular_size[index_l1_debug][index_l2_debug];
  // 
  // double l1 = pbi->l[index_l1_debug];
  // double l2 = pbi->l[index_l2_debug];
  // double l3;
  // 
  // fprintf (stderr, "### l1 = %g, l2 = %g\n", l1, l2);
  // printf ("### l1 = %g, l2 = %g\n", l1, l2);
      
  /* Node values */  
  // for (index_l3=index_l_triangular_min; index_l3<(index_l_triangular_min + l_triangular_size); ++index_l3) {  
  //   
  //   l3 = pbi->l[index_l3];
  //   double interpolated_bispectrum;
  //   class_call (bispectra_interpolate (pbi, 0, l1, l2, l3, &interpolated_bispectrum), pfi->error_message, pfi->error_message);
  // 
  //   double C_l1 = psp->cl[pwf->index_mode][(index_l1_debug * psp->ic_ic_size[pwf->index_mode] + index_ic1_ic2) * psp->ct_size + pwf->index_ct];
  //   double C_l2 = psp->cl[pwf->index_mode][(index_l2_debug * psp->ic_ic_size[pwf->index_mode] + index_ic1_ic2) * psp->ct_size + pwf->index_ct];
  //   double C_l3 = psp->cl[pwf->index_mode][(index_l3 * psp->ic_ic_size[pwf->index_mode] + index_ic1_ic2) * psp->ct_size + pwf->index_ct];
  //   double bispectrum = pbi->bispectra[0][index_l1_debug][index_l2_debug][index_l3-index_l_triangular_min]/sqrt(C_l1*C_l2*C_l3);
  //   
  //   fprintf (stderr, "%g %g %g\n", l3, bispectrum, interpolated_bispectrum);
  // }
    
  /* Interpolation */
  // int L;
  //   
  // for (L=MAX(2, abs(l1-l2)); L<=MIN(pbi->l[pbi->l_size-1], l1+l2); L+=2) {  
  //   
  //   double interpolated_bispectrum;
  //   
  //   class_call (bispectra_interpolate (pbi, 0, l1, l2, (double)L, &interpolated_bispectrum), pfi->error_message, pfi->error_message);
  //   
  //   fprintf (stderr, "%d %g\n", L, interpolated_bispectrum);
  // }  
  
  /* Check how much time it takes to interpolate the full bispectrum */
  // int delta = 5;
  // int counter = 0;
  // double interpolated_bispectrum;
  // 
  // int L1, L2, L3;
  // int L_MAX = pbi->l[pbi->l_size-1];
  // 
  // for (L1=2; L1 <= L_MAX; L1+=delta) {
  //   for (L2=2; L2 <= L_MAX; L2+=delta) {
  //     for (L3=MAX(2, abs(L1-L2)); L3 <= MIN(L_MAX, L1+L2); ++L3) {
  //       
  //       counter++;
  //       
  //       if ((L1+L2+L3)%2 !=0)
  //         continue;
  //       
  //       class_call (bispectra_interpolate (pbi, 0, L1, L2, L3, &interpolated_bispectrum), pfi->error_message, pfi->error_message)
  // 
  //     }
  //   }
  // }
  // 
  // printf("counter = %d\n", counter);


  return _SUCCESS_;

}



/**
 * Interpolate in a specific (l1,l2,l3) configuration the bispectrum corresponding to the
 * row of the Fisher matrix 'index_ft'.
 *
 * Note that the returned value needs to be divided by the window function. We do not do it
 * inside the interpolate function for optimization purposes.
 *
 */
 
int fisher_interpolate_bispectrum (
    struct bispectra * pbi,
    struct fisher * pfi,
    int index_ft,
    int X,
    int Y,
    int Z,
    double l1,
    double l2,
    double l3,
    double * interpolated_value
    )
{
  
  /* The analytical bispectra are never interpolated. In principle we could, but
  it does not make sense as it is quicker and more precise to just compute them. */
  class_test (pbi->bispectrum_type[pfi->index_bt_of_ft[index_ft]] == analytical_bispectrum,
    pbi->error_message,
    "cannot interpolate analytical bispectrum '%s'",
    pbi->bt_labels[pfi->index_bt_of_ft[index_ft]]);
  
  /* Use the fine mesh when all of the multipoles are small. */
  if ((l1<pfi->l_turnover[0]) && (l2<pfi->l_turnover[0]) && (l3<pfi->l_turnover[0])) {
    class_call (mesh_int (pfi->mesh_workspaces[index_ft][X][Y][Z][0], l1, l2, l3, interpolated_value),
    pfi->error_message, pfi->error_message);
  }
  /* Use the coarse mesh when any of the multipoles is large. */
  else {
    class_call (mesh_int (pfi->mesh_workspaces[index_ft][X][Y][Z][1], l1, l2, l3, interpolated_value),
    pfi->error_message, pfi->error_message);
  }
  
  /* Check for nan's */
  if (isnan(*interpolated_value))
    printf ("@@@ WARNING: Interpolated b(%g,%g,%g) = %g for bispectrum %s_%s!!!\n",
    l1, l2, l3, *interpolated_value,
    pbi->bt_labels[pfi->index_bt_of_ft[index_ft]],
    pbi->bfff_labels[X][Y][Z]);
  
  return _SUCCESS_;
  
}



/**
 * Compute the Fisher matrix for all pairs of bispectra.
 *
 * Called by bispectra_init.
 *
 */

int fisher_compute (
        struct precision * ppr,
        struct background * pba,
        struct perturbs * ppt,
        struct bessels * pbs,
        struct transfers * ptr,
        struct primordial * ppm,
        struct spectra * psp,
        struct lensing * ple,
        struct bispectra * pbi,
        struct fisher * pfi
        )
{
 
  /* Allocate workspace needed to estimate f_NL */
  struct fisher_workspace * pwf;
  class_alloc (pwf, sizeof(struct fisher_workspace), pfi->error_message);

  // ===============================================================================================
  // =                                  Initialise working space                                   =
  // ===============================================================================================

  /* Parallelization variables */
  int number_of_threads = 1;
  int thread = 0;
  int abort = _FALSE_;
  
  #ifdef _OPENMP
  #pragma omp parallel private (thread)
  number_of_threads = omp_get_num_threads();
  #endif
    
  /* Interpolation weights for the estimator in the rectangular directions (l1 and l2) */
  class_alloc (pwf->delta_l, pbi->l_size*sizeof(double), pfi->error_message);

  /* For the "normal" directions l1 and l2, the weights are the usual ones of the trapezoidal rule
  for a discrete sum. */  
  pwf->delta_l[0] = (pbi->l[1] - pbi->l[0] + 1.)/2.;
  
  for (int index_l=1; index_l < pbi->l_size-1; ++index_l)
    pwf->delta_l[index_l] = (pbi->l[index_l+1] - pbi->l[index_l-1])/2.;
      
  pwf->delta_l[pbi->l_size-1] = (pbi->l[pbi->l_size-1] - pbi->l[pbi->l_size-2] + 1.)/2.;


  /* Allocate memory for the interpolation weights in the triangular direction (l3).
  These are dependent on (l1,l2) and will therefore be computed inside the estimator
  for each (l1,l2) */
  if (pfi->bispectra_interpolation == smart_interpolation) {

    class_alloc (pwf->delta_l3, number_of_threads*sizeof(double*), pfi->error_message);

    #pragma omp parallel private (thread)
    {

      #ifdef _OPENMP
      thread = omp_get_thread_num();
      #endif
    
      class_calloc_parallel (pwf->delta_l3[thread], pbi->l_size, sizeof(double), pfi->error_message);

    } if (abort == _TRUE_) return _FAILURE_; // end of parallel region
  
  } // end of if (smart_interpolation)
  
  
  /* Print some info */
  if (pfi->fisher_verbose > 0) {

    char buffer[128];

    if (pfi->bispectra_interpolation == mesh_interpolation)
      strcpy (buffer, "mesh (3D)");
    else if (pfi->bispectra_interpolation == mesh_interpolation_2d)
      strcpy (buffer, "mesh (2D)");
    else if (pfi->bispectra_interpolation == trilinear_interpolation)
      strcpy (buffer, "trilinear");
    else if (pfi->bispectra_interpolation == sum_over_all_multipoles)
      strcpy (buffer, "no");
    else if (pfi->bispectra_interpolation == smart_interpolation)
      strcpy (buffer, "smart");

    printf (" -> computing Fisher matrix for l_max = %d with %s interpolation\n",
      MIN (pfi->l_max_estimator, pfi->l_max), buffer);
  }


  // ===============================================================================================
  // =                                   Compute Fisher matrix                                     =
  // ===============================================================================================
    

  /* Fill the "pfi->fisher_matrix_XYZ_l1" and "fisher_matrix_XYZ_l3" Fisher matrices using mesh interpolation */
  if ((pfi->bispectra_interpolation == mesh_interpolation)
  || (pfi->bispectra_interpolation == mesh_interpolation_2d)) {
  
    class_call (fisher_cross_correlate_mesh(
                  ppr,
                  psp,
                  ple,
                  pbi,
                  pfi,
                  pwf),
      pfi->error_message,
      pfi->error_message);
      
  }
  
  /* Fill the "pfi->fisher_matrix_XYZ_l1" and "fisher_matrix_XYZ_l3" Fisher matrices using linear interpolation */
  else if ((pfi->bispectra_interpolation == smart_interpolation)
  || (pfi->bispectra_interpolation == trilinear_interpolation)
  || (pfi->bispectra_interpolation == sum_over_all_multipoles)) {
            
    class_call (fisher_cross_correlate_nodes(
                  ppr,
                  psp,
                  ple,
                  pbi,
                  pfi,
                  pwf),
      pfi->error_message,
      pfi->error_message);
    
  } 
  
  else {

    class_stop (pfi->error_message, "could not understand bispectrum interpolation method!");

  }
  
  /* Account for partial sky coverage by multiplying the Fisher matrix by the sky fraction.
  Also, symmetrise the Fisher matrix. */
  for (int X = 0; X < pbi->bf_size; ++X) {
    for (int Y = 0; Y < pbi->bf_size; ++Y) {
      for (int Z = 0; Z < pbi->bf_size; ++Z) {
    
        for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
          for (int index_ft_2=index_ft_1; index_ft_2 < pfi->fisher_size; ++index_ft_2) {

            for (int index_l1=0; index_l1<pfi->l1_size; ++index_l1) {
              pfi->fisher_matrix_XYZ_l1[X][Y][Z][index_l1][index_ft_1][index_ft_2] *= pfi->f_sky;
              pfi->fisher_matrix_XYZ_l1[X][Y][Z][index_l1][index_ft_2][index_ft_1]
                = pfi->fisher_matrix_XYZ_l1[X][Y][Z][index_l1][index_ft_1][index_ft_2];   
            }
            
            /* Do the same for the Fisher matrix as a function of the smallest multipole */
            for (int index_l3=0; index_l3<pfi->l3_size; ++index_l3) {
              pfi->fisher_matrix_XYZ_l3[X][Y][Z][index_l3][index_ft_1][index_ft_2] *= pfi->f_sky;
              pfi->fisher_matrix_XYZ_l3[X][Y][Z][index_l3][index_ft_2][index_ft_1]
                = pfi->fisher_matrix_XYZ_l3[X][Y][Z][index_l3][index_ft_1][index_ft_2]; 
            }
          }
        }
      }
    }
  }

  /* Do the same for F_bar(l3,C,Z), which is needed to compute the lensing variance correction */
  if (pfi->include_lensing_effects == _TRUE_) {
    for (int i=0; i < pbi->bf_size*pfi->fisher_size; ++i) {
      for (int j=0; j < pbi->bf_size*pfi->fisher_size; ++j) {
        for (int index_l3=0; index_l3<pfi->l3_size; ++index_l3) {
          pfi->fisher_matrix_CZ_l3[index_l3][i][j] *= pfi->f_sky;
          pfi->fisher_matrix_CZ_l3[index_l3][j][i] = pfi->fisher_matrix_CZ_l3[index_l3][i][j];
          /* Debug - print F_bar */
          // fprintf (stderr, "%4d %4d %4d %17.7g\n",
          // pfi->l3[index_l3], i, j, pfi->fisher_matrix_CZ_l3[index_l3][j][i]);
        }
      }
    }
  }
  
  
  // ===============================================================================================
  // =                                   Compute lensing variance                                  =
  // ===============================================================================================

  /* Add the lensing variance to the Fisher matrix estimator, as explained in Section 5 of
  Lewis, Challinor & Hanson (2011, http://uk.arxiv.org/abs/1101.2234). See the documentation
  of 'fisher_lensing_variance' for further details */ 
  if (pfi->include_lensing_effects == _TRUE_) {

    /* Find the Fisher matrix including the noise induced by the CMB-lensing bispectrum.
    This function fills the array 'fisher_matrix_lensvar_l3' */
    class_call (fisher_lensing_variance (ppr,
                  pba,
                  ppt,
                  pbs,
                  ptr,
                  ppm,
                  psp,
                  ple,
                  pbi,
                  pfi),
      pfi->error_message,
      pfi->error_message);
   
    /* Find the total Fisher matrix by summing 'fisher_matrix_lensvar_l3' over l3 */
    for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2) {
  
        double accumulator = 0;
  
        for (int index_l3=(pfi->l3_size-1); index_l3>=0; --index_l3) {
  
          double l3_contribution = pfi->fisher_matrix_lensvar_l3[index_l3][index_ft_1][index_ft_2];
          accumulator += l3_contribution;
          pfi->fisher_matrix_lensvar_lmin[index_l3][index_ft_1][index_ft_2] = accumulator;
          if (index_ft_1 == index_ft_2)
            pfi->sigma_fnl_lensvar_lmin[index_l3][index_ft_1]
              = 1./sqrt(pfi->fisher_matrix_lensvar_lmin[index_l3][index_ft_1][index_ft_1]);
  
        } // end of for(index_l3)  
      } // end of for(ft_2)
    } // end of for (ft_1)
  
   for (int index_l3=0; index_l3<pfi->l3_size; ++index_l3)
     InverseMatrix (pfi->fisher_matrix_lensvar_lmin[index_l3],
       pfi->fisher_size, pfi->inverse_fisher_matrix_lensvar_lmin[index_l3]);
         
  } // end of lensing variance
  
  
  // ===============================================================================================
  // =                                     Compute S/N and f_nl                                    =
  // ===============================================================================================
  
  /* So far we have computed the Fisher matrix for each value of l_1; now we sum those values
  to obtain the Fisher matrix for each value of l_max */
  for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
    for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2) {

      /* We need to build the cumulative Fisher distribution, so we need accumulators. */
      double accumulator;
      double accumulator_XYZ[pbi->bf_size][pbi->bf_size][pbi->bf_size];

      /* Initialize the accumulators */
      accumulator = 0;
      for (int X = 0; X < pbi->bf_size; ++X)
        for (int Y = 0; Y < pbi->bf_size; ++Y)
          for (int Z = 0; Z < pbi->bf_size; ++Z)
            accumulator_XYZ[X][Y][Z] = 0.;

      // -----------------------------------------------------------------------
      // -                        As a function of lmax                        -
      // -----------------------------------------------------------------------
      
      /* Build the Fisher matrix as a function of lmax, as
      sum_{lmin<=l1<=lmax} fisher_matrix_XYZ_l1 */
      for (int index_l1=0; index_l1<pfi->l1_size; ++index_l1) {

        for (int X = 0; X < pbi->bf_size; ++X) {
          for (int Y = 0; Y < pbi->bf_size; ++Y) {
            for (int Z = 0; Z < pbi->bf_size; ++Z) {

              /* Contribution from this l1 to the total (X+Y+Z) Fisher matrix */
              double l1_contribution = pfi->fisher_matrix_XYZ_l1[X][Y][Z][index_l1][index_ft_1][index_ft_2];
                    
              /* Sum over  all possible XYZ */
              pfi->fisher_matrix_l1[index_l1][index_ft_1][index_ft_2] += l1_contribution;

              /* Include the interpolation weight for the l1 direction, if needed, in order
              to correcly sum over l1 (not needed for the sum over XYZ, above) */
              if (pfi->bispectra_interpolation == mesh_interpolation_2d)
                l1_contribution *= pwf->delta_l[index_l1];

              /* Contribution of all the l's smaller than l1 to the total (X+Y+Z) Fisher matrix */
              accumulator += l1_contribution;
              pfi->fisher_matrix_lmax[index_l1][index_ft_1][index_ft_2] = accumulator;

              /* Contribution of all the l's smaller than l1 to the Fisher matrix of XYZ */
              accumulator_XYZ[X][Y][Z] += l1_contribution;
              pfi->fisher_matrix_XYZ_lmax[X][Y][Z][index_l1][index_ft_1][index_ft_2] = accumulator_XYZ[X][Y][Z];

            } // Z
          } // Y
        } // X
            
        /* Detectability of the bispectrum, assuming that the bispectrum is a template with f_nl = 1. */
        if (index_ft_1 == index_ft_2)
          pfi->sigma_fnl_lmax[index_l1][index_ft_1] = 1./sqrt(pfi->fisher_matrix_lmax[index_l1][index_ft_1][index_ft_1]);

        /* Print fnl(l_max) */
        // fprintf (stderr, "%12d %12g\n", pfi->l1[index_l1], pfi->sigma_fnl_lmax[index_l1][0]);

      } // end of for(index_l1)
            
      /* Compute the inverse Fisher matrix */
      for (int index_l1=0; index_l1<pfi->l1_size; ++index_l1)
        InverseMatrix (pfi->fisher_matrix_lmax[index_l1], pfi->fisher_size, pfi->inverse_fisher_matrix_lmax[index_l1]);

            
      // -----------------------------------------------------------------------------
      // -                           As a function of lmin                           -
      // -----------------------------------------------------------------------------
        
      /* Build the Fisher matrix as a function of lmin, as
      sum_{lmin<=l3<=lmax} fisher_matrix_XYZ_l3 */
      accumulator = 0;
      for (int X = 0; X < pbi->bf_size; ++X)
        for (int Y = 0; Y < pbi->bf_size; ++Y)
          for (int Z = 0; Z < pbi->bf_size; ++Z)
            accumulator_XYZ[X][Y][Z] = 0.;

      for (int index_l3=(pfi->l3_size-1); index_l3>=0; --index_l3) {

        /* When including the lensing variance, the squeezed kernel is used in the Fisher matrix. This
        has a C_l^{Z\phi} factored out and therefore is much larger than any other bispectrum. In
        'fisher_lensing_variance' we have rescaled 'fisher_matrix_CZ_l3' so that it includes this C_l,
        and therefore it contains the actual squeezed CMB-lensing bispectrum rather than its kernel.
        The rescaling cannot be performed for the other Fisher arrays because they don't have information
        on the field C in the Fisher matrix sum. Therefore, we use 'fisher_matrix_CZ_l3' rather than
        'fisher_matrix_XYZ_l3' to build the Fisher matrix. */
        if (pfi->include_lensing_effects == _TRUE_) {

          for (int C=0; C < pbi->bf_size; ++C) {
            for (int Z=0; Z < pbi->bf_size; ++Z) {
                  
              double l3_contribution = pfi->fisher_matrix_CZ_l3[index_l3][index_ft_1*pbi->bf_size+C][index_ft_2*pbi->bf_size+Z];
              pfi->fisher_matrix_l3[index_l3][index_ft_1][index_ft_2] += l3_contribution;
              accumulator += l3_contribution;
              pfi->fisher_matrix_lmin[index_l3][index_ft_1][index_ft_2] = accumulator;
  
            } // Z
          } // C
        } // if lensing effects
        else {

          for (int X = 0; X < pbi->bf_size; ++X) {
            for (int Y = 0; Y < pbi->bf_size; ++Y) {
              for (int Z = 0; Z < pbi->bf_size; ++Z) {
        
                double l3_contribution = pfi->fisher_matrix_XYZ_l3[X][Y][Z][index_l3][index_ft_1][index_ft_2];
                pfi->fisher_matrix_l3[index_l3][index_ft_1][index_ft_2] += l3_contribution;
                accumulator += l3_contribution;
                pfi->fisher_matrix_lmin[index_l3][index_ft_1][index_ft_2] = accumulator;
                accumulator_XYZ[X][Y][Z] += l3_contribution;
                pfi->fisher_matrix_XYZ_lmin[X][Y][Z][index_l3][index_ft_1][index_ft_2] = accumulator_XYZ[X][Y][Z];
        
              } // Z
            } // Y
          } // X
        } // if not lensing effects
        
        if (index_ft_1 == index_ft_2)
          pfi->sigma_fnl_lmin[index_l3][index_ft_1] = 1./sqrt(pfi->fisher_matrix_lmin[index_l3][index_ft_1][index_ft_1]);

        /* Print fnl(l_min) */
        // fprintf (stderr, "%12d %12g\n", pfi->l3[index_l3], pfi->sigma_fnl_lmin[index_l3][0]);

      } // end of for(index_l3)
      
    } // end of for(ft_2)
  } // end of for (ft_1)
    
  for (int index_l3=0; index_l3<pfi->l3_size; ++index_l3)
    InverseMatrix (pfi->fisher_matrix_lmin[index_l3], pfi->fisher_size, pfi->inverse_fisher_matrix_lmin[index_l3]);

  /* Simple consistency check: the sum from above (l1) should be equal to the sum from below (l3).
  The test cannot be performed for the lensing variance row/column of the Fisher matrix because,
  in that case, the l1 arrays are not rescaled while the l3 ones are. */
  for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
    for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2) {

      if ((pfi->include_lensing_effects==_TRUE_)
      && ((index_ft_1==pfi->index_ft_cmb_lensing_kernel) || (index_ft_2==pfi->index_ft_cmb_lensing_kernel)))
        continue;
       
      double diff = 1-pfi->fisher_matrix_lmax[pfi->l1_size-1][index_ft_1][index_ft_2]
        /pfi->fisher_matrix_lmin[0][index_ft_1][index_ft_2];
      
      class_test (fabs (diff) > _SMALL_,
        pfi->error_message,
        "error in the Fisher matrix sum, diff=%g.", diff);
    }
  }

  /* Print the signal to noise as a function of l_max for all the analised bispectra */
  // fprintf (stderr, "%4s ", "l");
  // for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft)    
  //   fprintf (stderr, "%20s ", pbi->bt_labels[pfi->index_bt_of_ft[index_ft]]);
  // fprintf (stderr, "\n");
  // 
  // for (int index_l1=0; index_l1<pfi->l1_size; ++index_l1) {
  //   fprintf (stderr, "%4d ", pfi->l1[index_l1]);
  //   for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft)
  //     fprintf (stderr, "%20.7g ", sqrt(pfi->fisher_matrix_lmax[index_l1][index_ft][index_ft]));
  //   fprintf (stderr, "\n");
  // }



  // ===============================================================================================
  // =                                        Print results                                        =
  // ===============================================================================================
    
  /* Print the Fisher matrix for all the bispectra (TTT,TTE,TET...). This tells us which bispectrum
  contributes the most to the total signal-to-noise. */
  if ((pfi->fisher_verbose > 2) && (pbi->bf_size > 1)) {
  
    for (int X = 0; X < pbi->bf_size; ++X) {
      for (int Y = 0; Y < pbi->bf_size; ++Y) {
        for (int Z = 0; Z < pbi->bf_size; ++Z) {
  
          sprintf (pfi->info, "%s     * %s contribution (percent)\n", pfi->info, pbi->bfff_labels[X][Y][Z]);
  
          for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      
            sprintf (pfi->info, "%s\t%20s\t(", pfi->info, "");
      
            for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2)
              sprintf (pfi->info, "%s %8.3g ", pfi->info, pfi->fisher_matrix_XYZ_lmax[X][Y][Z][pfi->l1_size-1][index_ft_1][index_ft_2]/
                pfi->fisher_matrix_lmin[0][index_ft_1][index_ft_2]*100);
  
            sprintf (pfi->info, "%s)\n", pfi->info);
          }
        }
      }
    }
  }
    
  /* Print the Fisher matrix */
  if (pfi->fisher_verbose > 0) {

    sprintf (pfi->info, "%s -> Fisher matrix for l_max = %d:\n",
      pfi->info, MIN (pfi->l_max_estimator, pfi->l_max));
    
    for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      
      sprintf (pfi->info, "%s\t%20s\t", pfi->info, pbi->bt_labels[pfi->index_bt_of_ft[index_ft_1]]);
      
      sprintf (pfi->info, "%s(", pfi->info);
      
      for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2)
        sprintf (pfi->info, "%s %11.4g ", pfi->info, pfi->fisher_matrix_lmin[0][index_ft_1][index_ft_2]);

      sprintf (pfi->info, "%s)\n", pfi->info);
    }

    if (pfi->include_lensing_effects == _TRUE_) {
      
      sprintf (pfi->info, "%s -> Fisher matrix for l_max = %d, INCLUDING LENSING NOISE:\n",
        pfi->info, MIN (pfi->l_max_estimator, pfi->l_max));
    
      for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      
        sprintf (pfi->info, "%s\t%20s\t", pfi->info, pbi->bt_labels[pfi->index_bt_of_ft[index_ft_1]]);
      
        sprintf (pfi->info, "%s(", pfi->info);
      
        for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2)
          sprintf (pfi->info, "%s %11.4g ", pfi->info, pfi->fisher_matrix_lensvar_lmin[0][index_ft_1][index_ft_2]);

        sprintf (pfi->info, "%s)\n", pfi->info);
      }
    }
  }
  
  /* Print the correlation matrix */
  if ((pfi->fisher_verbose > 0) && (pfi->fisher_size>1)) {

    sprintf (pfi->info, "%s -> Correlation matrix for l_max = %d:\n",
      pfi->info, MIN (pfi->l_max_estimator, pfi->l_max));
    
    for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      
      sprintf (pfi->info, "%s\t%20s\t", pfi->info, pbi->bt_labels[pfi->index_bt_of_ft[index_ft_1]]);
      
      sprintf (pfi->info, "%s(", pfi->info);
      
      for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2)
        sprintf (pfi->info, "%s %11.4g ", pfi->info,
        pfi->fisher_matrix_lmin[0][index_ft_1][index_ft_2]
        /sqrt(pfi->fisher_matrix_lmin[0][index_ft_1][index_ft_1]
        *pfi->fisher_matrix_lmin[0][index_ft_2][index_ft_2]));

      sprintf (pfi->info, "%s)\n", pfi->info);
    }

    if (pfi->include_lensing_effects == _TRUE_) {
      
      sprintf (pfi->info, "%s -> Correlation matrix for l_max = %d, INCLUDING LENSING NOISE:\n",
        pfi->info, MIN (pfi->l_max_estimator, pfi->l_max));
    
      for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      
        sprintf (pfi->info, "%s\t%20s\t", pfi->info, pbi->bt_labels[pfi->index_bt_of_ft[index_ft_1]]);
      
        sprintf (pfi->info, "%s(", pfi->info);
      
        for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2)
          sprintf (pfi->info, "%s %11.4g ", pfi->info,
          pfi->fisher_matrix_lensvar_lmin[0][index_ft_1][index_ft_2]
          /sqrt(pfi->fisher_matrix_lensvar_lmin[0][index_ft_1][index_ft_1]
          *pfi->fisher_matrix_lensvar_lmin[0][index_ft_2][index_ft_2]));

        sprintf (pfi->info, "%s)\n", pfi->info);
      }
    }
  }
  
  /* Print the inverse matrix */
  if (pfi->fisher_verbose > 0) {
  
    sprintf (pfi->info, "%s -> Reciprocal of the inverse Fisher matrix:\n", pfi->info);
    
    for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      
      sprintf (pfi->info, "%s\t%20s\t", pfi->info, pbi->bt_labels[pfi->index_bt_of_ft[index_ft_1]]);
      
      sprintf (pfi->info, "%s(", pfi->info);
      
      for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2)
        sprintf (pfi->info, "%s %11.4g ", pfi->info, 1/pfi->inverse_fisher_matrix_lmin[0][index_ft_1][index_ft_2]);
  
      sprintf (pfi->info, "%s)\n", pfi->info);
    }
    
    if (pfi->include_lensing_effects == _TRUE_) {
    
      sprintf (pfi->info, "%s -> Reciprocal of the inverse Fisher matrix, INCLUDING LENSING NOISE:\n", pfi->info);
    
      for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      
        sprintf (pfi->info, "%s\t%20s\t", pfi->info, pbi->bt_labels[pfi->index_bt_of_ft[index_ft_1]]);
      
        sprintf (pfi->info, "%s(", pfi->info);
      
        for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2)
          sprintf (pfi->info, "%s %11.4g ", pfi->info, 1/pfi->inverse_fisher_matrix_lensvar_lmin[0][index_ft_1][index_ft_2]);
  
        sprintf (pfi->info, "%s)\n", pfi->info);
      }
    }
  }
  
  /* Print the fnl matrix */
  if (pfi->fisher_verbose > 0) {
  
    sprintf (pfi->info, "%s -> fnl matrix (diagonal: 1/sqrt(F_ii), upper: F_12/F_11, lower: F_12/F_22):\n", pfi->info);
    
    for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      
      sprintf (pfi->info, "%s\t%20s\t", pfi->info, pbi->bt_labels[pfi->index_bt_of_ft[index_ft_1]]);
      
      sprintf (pfi->info, "%s(", pfi->info);
      
      for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2) {
  
        /* Diagonal elements = 1/sqrt(F_ii) */ 
        if (index_ft_1==index_ft_2)
          sprintf (pfi->info, "%s %11.4g ", pfi->info, 1./sqrt(pfi->fisher_matrix_lmin[0][index_ft_1][index_ft_1]));
        /* Upper triangle = F_12/F_11, lower triangle = F_12/F_22. */
        else
          sprintf (pfi->info, "%s %11.4g ", pfi->info, pfi->fisher_matrix_lmin[0][index_ft_1][index_ft_2]
            /pfi->fisher_matrix_lmin[0][index_ft_1][index_ft_1]);
      }
      sprintf (pfi->info, "%s)\n", pfi->info);
    }
    
    if (pfi->include_lensing_effects == _TRUE_) {
    
      sprintf (pfi->info, "%s -> fnl matrix, INCLUDING LENSING NOISE:\n", pfi->info);
    
      for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      
        sprintf (pfi->info, "%s\t%20s\t", pfi->info, pbi->bt_labels[pfi->index_bt_of_ft[index_ft_1]]);
      
        sprintf (pfi->info, "%s(", pfi->info);
      
        for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2) {
  
          /* Diagonal elements = 1/sqrt(F_ii) */ 
          if (index_ft_1==index_ft_2)
            sprintf (pfi->info, "%s %11.4g ", pfi->info, 1./sqrt(pfi->fisher_matrix_lensvar_lmin[0][index_ft_1][index_ft_1]));
          /* Upper triangle = F_12/F_11, lower triangle = F_12/F_22. */
          else
            sprintf (pfi->info, "%s %11.4g ", pfi->info, pfi->fisher_matrix_lensvar_lmin[0][index_ft_1][index_ft_2]
              /pfi->fisher_matrix_lensvar_lmin[0][index_ft_1][index_ft_1]);
        }
        sprintf (pfi->info, "%s)\n", pfi->info);
      }
    }
  }
  
  /* Print to screen the Fisher matrix */
  printf ("%s", pfi->info);
  
  return _SUCCESS_;
  
}






/**
 *
 * This function computes the Fisher matrix and fills pfi->fisher_matrix_XYZ_l1 and 
 * pfi->fisher_matrix_XYZ_l3 using a linear interpolation of the bispectrum.
 */
int fisher_cross_correlate_nodes (
       struct precision * ppr,
       struct spectra * psp,
       struct lensing * ple,
       struct bispectra * pbi,
       struct fisher * pfi,
       struct fisher_workspace * pwf
       )
{  

  /* We shall count the (l1,l2,l3) configurations over which we compute the estimator */
  long int counter = 0;

  // =======================================================================================
  // =                            Compute Fisher matrix elements                           =
  // =======================================================================================

  /* Parallelization variables */
  int thread = 0;
  int abort = _FALSE_;
  
  #pragma omp parallel private (thread)
  {
    #ifdef _OPENMP
    thread = omp_get_thread_num();
    #endif
  
    // ------------------------------------------------
    // -                  Sum over l1                 -
    // ------------------------------------------------

    #pragma omp for schedule (dynamic)
    for (int index_l1=0; index_l1<pfi->l1_size; ++index_l1) {
    
      int l1 = pfi->l1[index_l1];
    
      if ((l1 < pfi->l1_min_global) || (l1 > pfi->l1_max_global))
        continue;
    
      if (pfi->fisher_verbose > 2)
        printf ("     * computing Fisher matrix for l1=%d\n", l1);

      // ------------------------------------------------
      // -                  Sum over l2                 -
      // ------------------------------------------------
    
      for (int index_l2=0; index_l2<=index_l1; ++index_l2) {
        
        int l2 = pfi->l2[index_l2];
  
        if ((l2 < pfi->l2_min_global) || (l2 > pfi->l2_max_global))
          continue;
        
        /* Skip those configurations that are forbidden by the triangular condition (optimization) */
        if (l2 < l1/2)
          continue;
        
        /* Determine the limits for l3, which come from the triangular inequality |l1-l2| <= l3 <= l1+l2 */
        int index_l3_min = pbi->index_l_triangular_min[index_l1][index_l2];
        int index_l3_max = MIN (index_l2, pbi->index_l_triangular_max[index_l1][index_l2]);
        
        if (pfi->bispectra_interpolation == smart_interpolation) {

          class_call_parallel (fisher_interpolation_weights(
                                 ppr,
                                 psp,
                                 ple,
                                 pbi,
                                 pfi,
                                 index_l1,
                                 index_l2,
                                 pwf->delta_l3[thread],
                                 pwf),
            pfi->error_message,
            pfi->error_message);
        }

        // ------------------------------------------------
        // -                  Sum over l3                 -
        // ------------------------------------------------

        for (int index_l3=index_l3_min; index_l3<=index_l3_max; ++index_l3) {
        
          int l3 = pfi->l3[index_l3];
                                
          if ((l3 < pfi->l3_min_global) || (l3 > pfi->l3_max_global))
            continue;

          /* Parity invariance enforces that the contribution of the modes with odd l1+l2+l3 is zero. Even if you
          comment this out, the result won't change as the I_l1_l2_l3 coefficient would vanish in that case. */
          if ((l1+l2+l3)%2!=0) {
            #pragma omp atomic
            counter++;
            continue;
          }

          /* Index of the current (l1,l2,l3) configuration */
          long int index_l1_l2_l3 = pbi->index_l1_l2_l3[index_l1][index_l1-index_l2][index_l3_max-index_l3];

          // ------------------------------------------------------------------------
          // -                        Build the Fisher matrix                       -
          // ------------------------------------------------------------------------

          /* To compute the covariance matrix, we use the formula first given in eq. 17 of Yadav,
          Komatsu & Wandelt 2007 (http://uk.arxiv.org/abs/astro-ph/0701921), which is a numerical
          improvement over the treatment in Babich & Zaldarriaga 2004. (Note that explicit form
          given in appendix B of the same paper is valid only when l1=l2=l3, as noted in appendix E
          of Lewis, Challinor & Hanson 2011.)
          For the case where only one probe is requested, the below loops reduce to one iteration
          over a single bispectrum (eg. T -> TTT). When two probes are requested, each loop corresponds
          to one of 2^3 bispectra (eg. TTT,TTE,TET,ETT,TEE,ETE,EET,EEE), for a total of 64 elements in
          the covariance matrix. */
          
          /* Shortcuts to the inverse of the cross-power spectrum */
          double ** C1 = pfi->inverse_C[l1-2];
          double ** C2 = pfi->inverse_C[l2-2];
          double ** C3 = pfi->inverse_C[l3-2];
          
          /* Weights for the interpolation. Its default value is unity, in case interpolation is not
          needed. */
          double interpolation_weight = 1;
          
          if (pfi->bispectra_interpolation == smart_interpolation) {
            interpolation_weight = pwf->delta_l[index_l1] * pwf->delta_l[index_l2] * pwf->delta_l3[thread][index_l3];
          }
          else if (pfi->bispectra_interpolation == trilinear_interpolation) {
            /* Weight for trilinear interpolation. When the trilinear interpolation is turned on, we always consider
            configurations where all l's are even. The factor 1/2 comes from the fact that when interpolating over even
            configurations only, we are implicitly assuming non-zero values for the odd points, which instead are
            forced to vanish by the 3J. (Note that this factor, in the case of the smart interpolation, is already
            included in pwf->delta_l3) */
            interpolation_weight = 0.5 * pwf->delta_l[index_l1] * pwf->delta_l[index_l2] * pwf->delta_l[index_l3];
          }
          else if ((pfi->bispectra_interpolation == sum_over_all_multipoles) && (ppr->compute_only_even_ls == _TRUE_)) {
            /* If we are summing over all multipoles, and we have only computed configurations where all l's are
            even, then we need to multiply the final result by 2. This is equivalent to assuming that combinations
            like 2,3,3 (which are not computed if ppr->compute_only_even_ls == _TRUE_) give to the integral the same
            contribution as 2,2,2. I have verified that the error is about ~ 1 permille for the local template, by
            comparing an 'all_even run times 2' with a full run. The error is O(1) for bispectra than involve
            3j's with 2's in the bottom line. */
            interpolation_weight *= 2;
          }
          
          /* If we are taking all the l-points, then any interpolation scheme reduces to a simple sum over the
          multipoles, and then we can naturally implement the delta factor in KSW2005. */
          double one_over_delta = 1;
          if (ppr->l_linstep == 1) {

            if ((l1==l2) && (l1==l3))
              one_over_delta = 1/6.;

            else if ((l1==l2) || (l1==l3) || (l2==l3))
              one_over_delta = 0.5;
          }
          
          /* Include the double 3j symbol */
          double threej_000_squared = pfi->I_l1_l2_l3[index_l1_l2_l3] * pfi->I_l1_l2_l3[index_l1_l2_l3];
          
          /* Compute the Fisher matrix. In the simple case of only one probe (eg. temperature -> TTT),
          each element of the Fisher matrix is simply given by the square of the bispectrum divided
          by three C_l's. When dealing with two probes (eg. T and E -> TTT,TTE,TET,ETT,TEE,ETE,EET,EEE),
          each element of the Fisher matrix, contains a quadratic sum over the 8 available bispectra,
          for a total of 64 terms. */
          for (int X = 0; X < pbi->bf_size; ++X) { for (int A = 0; A < pbi->bf_size; ++A) {
          for (int Y = 0; Y < pbi->bf_size; ++Y) { for (int B = 0; B < pbi->bf_size; ++B) {
          for (int Z = 0; Z < pbi->bf_size; ++Z) { for (int C = 0; C < pbi->bf_size; ++C) {

            double inverse_covariance = C1[A][X] * C2[B][Y] * C3[C][Z];

            for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
              for (int index_ft_2=index_ft_1; index_ft_2 < pfi->fisher_size; ++index_ft_2) {              

                double fisher = one_over_delta *
                                interpolation_weight *
                                threej_000_squared *
                                inverse_covariance *
                                pbi->bispectra[pfi->index_bt_of_ft[index_ft_1]][A][B][C][index_l1_l2_l3] *                             
                                pbi->bispectra[pfi->index_bt_of_ft[index_ft_2]][X][Y][Z][index_l1_l2_l3];
                                
                #pragma omp atomic
                pfi->fisher_matrix_XYZ_l1[X][Y][Z][index_l1][index_ft_1][index_ft_2] += fisher;
                #pragma omp atomic
                pfi->fisher_matrix_XYZ_l3[X][Y][Z][index_l3][index_ft_1][index_ft_2] += fisher;
                  
              } // bt_2
            } // bt_1
          }}} // XYZ
          }}} // ABC
                
          #pragma omp atomic
          counter++;          

        } // end of for(index_l3)
      } // end of for(index_l2)
    } // end of for(index_l1)
  } if (abort == _TRUE_) return _FAILURE_; // end of parallel region
      
  return _SUCCESS_;
  
}




int fisher_interpolation_weights (
       struct precision * ppr,
       struct spectra * psp,
       struct lensing * ple,
       struct bispectra * pbi,
       struct fisher * pfi,
       int index_l1,
       int index_l2,
       double * delta_l3,
       struct fisher_workspace * pwf
       )
{  
  
  int l1 = pbi->l[index_l1];
  int l2 = pbi->l[index_l2];

  /* Limits of our l3 sampling */
  int index_l3_min = pbi->index_l_triangular_min[index_l1][index_l2];
  int index_l3_max = MIN (index_l2, pbi->index_l_triangular_max[index_l1][index_l2]);
      
  // -----------------------------------------------------------------------------------------
  // -                            Exclude points with l1+l2+l3 odd                           -
  // -----------------------------------------------------------------------------------------
      
  // if ((l1==40) && (l2==40)) {
  //   printf("(l1=%d,l2=%d) BEFORE:", l1, l2);
  //   for (int index_l3=index_l3_min; index_l3<=index_l3_max; ++index_l3)
  //     printf("%d,",pbi->l[index_l3]);
  //   printf("\n");
  // }
      
  /* Restrict the l3 range so that the first and last points are both even (if l1+l2 is even) or odd
  (if l1+l2 is odd). If this cannot be achieved, it means that our l-sampling fails to capture a valid
  point for the considered (l1,l2) pair. Then, the Fisher matrix computation will neglect this pair. */
  if ((l1+l2)%2==0) {
    while ((pbi->l[index_l3_min]%2!=0) && (index_l3_min<=index_l3_max))
      index_l3_min++;
    while ((pbi->l[index_l3_max]%2!=0) && (index_l3_max>=index_l3_min))
      index_l3_max--;
  }
  else {
    while ((pbi->l[index_l3_min]%2==0) && (index_l3_min<=index_l3_max))
      index_l3_min++;
    while ((pbi->l[index_l3_max]%2==0) && (index_l3_max>=index_l3_min))
      index_l3_max--;
  }

  // if ((l1==40) && (l2==40)) {
  //   printf("(l1=%d,l2=%d) AFTER:", l1, l2);
  //   for (int index_l3=index_l3_min; index_l3<=index_l3_max; ++index_l3)
  //     printf("%d,",pbi->l[index_l3]);
  //   printf("\n");
  // }

  // ------------------------------------------------------------------------------------------
  // -                                Generate linear weights                                 -
  // ------------------------------------------------------------------------------------------
  
  /* First, we generate "normal" interpolation weights, as if the l3-range was rectangular.
  We also include an extra 1/2 factor to account for the fact that the Fisher estimator
  only includes those configuration where l1+l2+l3 is even. */
  /* TODO: This formula is valid only if l1+l2 is even. When l1+l2 is odd, then l3 must be
  odd to and the formula has to be adjusted */
  if (index_l3_min < index_l3_max) {

    delta_l3[index_l3_min] = (pbi->l[index_l3_min+1] - pbi->l[index_l3_min] + 2.)/4.;
      
    for (int index_l=index_l3_min+1; index_l<=(index_l3_max-1); ++index_l)
      delta_l3[index_l] = (pbi->l[index_l+1] - pbi->l[index_l-1])/4.;
      
    delta_l3[index_l3_max] = (pbi->l[index_l3_max] - pbi->l[index_l3_max-1] + 2.)/4.;

   if (index_l3_min == index_l3_max)
    delta_l3[index_l3_min] = 1;
  }
  /* If only one point is valid, its weight must be unity */
  else if (index_l3_min == index_l3_max) {
    delta_l3[index_l3_min] = 1;
  }
  /* If there are no usable points at all, it really does not matter what we do here, because the (l1,l2)
  configuration will be skipped anyway during the Fisher matrix l-sum. */
  else {
    return _SUCCESS_;
  }

  /* Some debug */
  // for (int index_l=index_l3_min; index_l <= (index_l3_max); ++index_l)
  //   printf ("delta_l3[index_l] = %g\n", delta_l3[index_l]);

  
  // ----------------------------------------------------------------------------------
  // -                          Account for out-of-bonds points                       -
  // ----------------------------------------------------------------------------------
  
  /* We assign to those points outside our l3-sampling (but inside the triangular condition) the
  bispectrum-value in l3. To do so, however, we need to properly count which of these
  out-of-bounds points are even and odd */
  for (int l3 = MAX(pfi->l_min, abs(l1-l2)); l3 < pbi->l[index_l3_min]; ++l3)
    if((l1+l2+l3)%2==0)
      delta_l3[index_l3_min] += 1.;
  
  for (int l3 = pbi->l[index_l3_max]+1; l3 <= MIN(l2,l1+l2); ++l3)
    if((l1+l2+l3)%2==0)
      delta_l3[index_l3_max] += 1;
  
  /* Check that, for a function that always evaluates to one, the weights sum up to the number
  of even/odd points in the integration range. For a given pair of (l1,l2), the range goes
  from MAX(2,|l1-l2|) to MIN(l2,l1+l2). 
  Remember that now 'index_l3_min' and 'index_l3_max' have been modified with respect to those in
  pbi->index_l_triangular_min and pbi->index_l_triangular_max to be even/odd. */
  double sum = 0;
  for (int index_l3=index_l3_min; index_l3<=index_l3_max; ++index_l3)
    sum += delta_l3[index_l3];
  
  int n_even=0, n_odd=0;
  for (int l3 = MAX(pfi->l_min,abs(l1-l2)); l3 <= MIN(l2,l1+l2); ++l3) {
    if(l3%2==0) n_even++;
    else n_odd++;
  }
  
  if ((l1+l2)%2==0) {
    class_test (sum != n_even,
      pfi->error_message,
      "EVEN: l1=%d, l2=%d, sum=%g, expected=%d\n", l1, l2, sum, n_even);
  }
  else {
    class_test (sum != n_odd,
      pfi->error_message,
      "ODD:  l1=%d, l2=%d, sum=%g, expected=%d\n", l1, l2, sum, n_odd);
  }

  return _SUCCESS_;

}













/**
 *
 * This function computes the Fisher matrix and fills pfi->fisher_matrix_XYZ_l1 and 
 * pfi->fisher_matrix_XYZ_l3 using a mesh interpolation of the bispectrum.
 */
int fisher_cross_correlate_mesh (
       struct precision * ppr,
       struct spectra * psp,
       struct lensing * ple,
       struct bispectra * pbi,
       struct fisher * pfi,
       struct fisher_workspace * pwf
       )
{  

  /* We shall count the (l1,l2,l3) configurations over which we compute the estimator */
  long int counter = 0;

  // =======================================================================================
  // =                            Compute Fisher matrix elements                           =
  // =======================================================================================

  /* Parallelization variables */
  int number_of_threads = 1;
  int thread = 0;
  int abort = _FALSE_;
  
  #pragma omp parallel private (thread)
  #ifdef _OPENMP
  number_of_threads = omp_get_num_threads();
  #endif

  /* Temporary values needed to store the bispectra interpolated in a given (l1,l2,l3) configuration 
  and for a certain type of bispectrum (e.g. local_tte). The array is indexed as
  interpolated_bispectra[thread][index_ft][X][Y][Z] */
  double ***** interpolated_bispectra;
  class_alloc (interpolated_bispectra, number_of_threads*sizeof(double *****), pfi->error_message);  

  #pragma omp parallel shared (abort,counter) private (thread)
  {
    
    #ifdef _OPENMP
    thread = omp_get_thread_num();
    #endif

    /* As many interpolations as the total number of bispectra (pfi->fisher_size * pbi->bf_size^3) */
    class_alloc_parallel (interpolated_bispectra[thread], pfi->fisher_size*sizeof(double ***), pfi->error_message);
    for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
      class_alloc_parallel (interpolated_bispectra[thread][index_ft], pbi->bf_size*sizeof(double **), pfi->error_message);
      for (int X = 0; X < pbi->bf_size; ++X) {
        class_alloc_parallel (interpolated_bispectra[thread][index_ft][X], pbi->bf_size*sizeof(double *), pfi->error_message);
        for (int Y = 0; Y < pbi->bf_size; ++Y)
          class_alloc_parallel (interpolated_bispectra[thread][index_ft][X][Y], pbi->bf_size*sizeof(double), pfi->error_message);
      }
    }


    // ------------------------------------------------
    // -                  Sum over l1                 -
    // ------------------------------------------------

    #pragma omp for schedule (dynamic)
    for (int index_l1=0; index_l1<pfi->l1_size; ++index_l1) {
    
      int l1 = pfi->l1[index_l1];
      double C_l1 = pbi->cls[pfi->index_ct_window][l1-2];
    
      if ((l1 < pfi->l1_min_global) || (l1 > pfi->l1_max_global))
        continue;

      /* Arrays that will contain the 3j symbols for a given (l1,l2) */
      double threej_000[2*pbi->l_max+1], threej_m220[2*pbi->l_max+1],
             threej_0m22[2*pbi->l_max+1], threej_20m2[2*pbi->l_max+1];

      /* First l-multipole stored in the above arrays */
      int l3_min_000=0, l3_min_0m22=0, l3_min_20m2=0, l3_min_m220=0;
    
      if (pfi->fisher_verbose > 2)
        printf ("     * computing Fisher matrix for l1=%d\n", l1);

      /* The triangular condition imposes l3 >= l1-l2, while the bound on the summation imposes l3 <= l2. Hence,
      the minimum allowed value for l2 is l1/2. */
      int l2_min = MAX(l1/2,2);

      // ------------------------------------------------
      // -                  Sum over l2                 -
      // ------------------------------------------------
    
      for (int l2=l2_min; l2<=l1; ++l2) {
  
        if ((l2 < pfi->l2_min_global) || (l2 > pfi->l2_max_global))
          continue;
      
        double C_l2 = pbi->cls[pfi->index_ct_window][l2-2];
  
        int l3_min = MAX(l1-l2,2);

        // -----------------------------------------------------------------------------------
        // -                             Compute three-j symbols                             -
        // -----------------------------------------------------------------------------------
      
        /* Compute the 3j-symbol that enters the estimator, (l1 l2 l3)(0 0 0) */
        
        double min_D, max_D;
  
        class_call_parallel (drc3jj (
                               l1, l2, 0, 0,
                               &min_D, &max_D,
                               threej_000,
                               (2*pbi->l_max+1),
                               pfi->error_message),
          pfi->error_message,
          pfi->error_message);          
        l3_min_000 = (int)(min_D + _EPS_);


        /* Compute more 3j-symbols, needed for specific bispectra. For more information
        on why these are needed, refer to the function 'bispectra_analytical_init' in the
        bispectrum module. */

        if (pbi->need_3j_symbols == _TRUE_) {            

          class_call_parallel (drc3jj (
                                 l1, l2, 0, -2,
                                 &min_D, &max_D,
                                 threej_0m22,
                                 (2*pbi->l_max+1),
                                 pfi->error_message),
            pfi->error_message,
            pfi->error_message);
          l3_min_0m22 = (int)(min_D + _EPS_);
    
          class_call_parallel (drc3jj (
                                 l1, l2, 2, 0,
                                 &min_D, &max_D,
                                 threej_20m2,
                                 (2*pbi->l_max+1),
                                 pfi->error_message),
            pfi->error_message,
            pfi->error_message);
          l3_min_20m2 = (int)(min_D + _EPS_);
        
          class_call_parallel (drc3jj (
                                 l1, l2, -2, 2,
                                 &min_D, &max_D,
                                 threej_m220,
                                 (2*pbi->l_max+1),
                                 pfi->error_message),
            pfi->error_message,
            pfi->error_message);
          l3_min_m220 = (int)(min_D + _EPS_);
    
        } // end of 3j computation

        // ------------------------------------------------
        // -                  Sum over l3                 -
        // ------------------------------------------------

        for (int l3=l3_min; l3<=l2; ++l3) {
                                
          if ((l3 < pfi->l3_min_global) || (l3 > pfi->l3_max_global))
            continue;

          /* Uncomment to consider only squeezed configurations */
          // if (!((l3<=100) && (l2>=10*l3))) {
          //   #pragma omp atomic
          //   counter++;
          //   continue;
          // }
        
          double C_l3 = pbi->cls[pfi->index_ct_window][l3-2];
        
          /* Parity invariance enforces that the contribution of the modes with odd l1+l2+l3 is zero. Even if you
          comment this out, the result won't change as the I_l1_l2_l3 coefficient would vanish in that case. */
          if ((l1+l2+l3)%2!=0) {
            #pragma omp atomic
            counter++;
            continue;
          }

          // ---------------------------------------------------------------------------
          // -                              Obtain bispectra                           -
          // ---------------------------------------------------------------------------

          /* Factor that relates the reduced bispectrum to the angle-averaged one */
          double I_l1_l2_l3 = sqrt((2.*l1+1.)*(2.*l2+1.)*(2.*l3+1.)/(4.*_PI_)) * threej_000[l3-l3_min_000];

          /* Obtain all the bispectra in this (l1,l2,l3) configuration, either via interpolation or
          by direct computation (if the bispectrum can be expressed in a simple analytical form).
          These loops go over all the possible bispectra, eg. local_ttt, equilateral_ete, intrinsic_ttt. */
          for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
            
            int index_bt = pfi->index_bt_of_ft[index_ft];
            
            for (int X = 0; X < pbi->bf_size; ++X) {
              for (int Y = 0; Y < pbi->bf_size; ++Y) {
                for (int Z = 0; Z < pbi->bf_size; ++Z) {
              
                  /* Compute analytical bispectra */
                  if (pbi->bispectrum_type[index_bt] == analytical_bispectrum) {
                
                    /* Check that the current bispectrum has a function associated to it */
                    class_test_parallel (pbi->bispectrum_function[index_bt]==NULL,
                      pbi->error_message,
                      "no function associated for the bispectrum '%s'. Maybe it's not analytical?",
                      pbi->bt_labels[index_bt]);

                    class_call_parallel ((*pbi->bispectrum_function[index_bt]) (
                                  ppr, psp, ple, pbi,
                                  l1, l2, l3,
                                  X, Y, Z,
                                  threej_000[l3-l3_min_000],
                                  threej_20m2[l3-l3_min_20m2],
                                  threej_m220[l3-l3_min_m220],
                                  threej_0m22[l3-l3_min_0m22],
                                  &interpolated_bispectra[thread][index_ft][X][Y][Z]),
                      pbi->error_message,
                      pbi->error_message);

                  } 
                  else {

                    /* Interpolate all other bispectra */
                    class_call_parallel (fisher_interpolate_bispectrum(
                                           pbi, pfi,
                                           index_ft,
                                           X,Y,Z,
                                           l1, l2, l3,
                                           &interpolated_bispectra[thread][index_ft][X][Y][Z]),
                      pfi->error_message,
                      pfi->error_message);

                    /* Compensate the effect of the window function */
                    double window = 1;
                
                    if (pbi->window_function[index_bt] != NULL)
                      class_call_parallel ((*pbi->window_function[index_bt]) (
                                    ppr, psp, ple, pbi,
                                    l1, l2, l3,
                                    X, Y, Z,
                                    threej_000[l3-l3_min_000],
                                    threej_20m2[l3-l3_min_20m2],
                                    threej_m220[l3-l3_min_m220],
                                    threej_0m22[l3-l3_min_0m22],
                                    &window),
                        pbi->error_message,
                        pbi->error_message);

                    // double inverse_window = 1;
                    // double inverse_window = C_l1*C_l2 + C_l1*C_l3 + C_l2*C_l3;
                    interpolated_bispectra[thread][index_ft][X][Y][Z] /= window;
                  } 
              
                  /* All bispectra have to be multiplied by the parity factor */
                  interpolated_bispectra[thread][index_ft][X][Y][Z] *= I_l1_l2_l3;

                } // end of for(Z)
              } // end of for(Y)
            } // end of for(X)
          } // end of for(index_ft)
          

          // ------------------------------------------------------------------------
          // -                        Build the Fisher matrix                       -
          // ------------------------------------------------------------------------

          /* To compute the covariance matrix, we use the formula first given in eq. 17 of Yadav,
          Komatsu & Wandelt 2007 (http://uk.arxiv.org/abs/astro-ph/0701921), which is a numerical
          improvement over the treatment in Babich & Zaldarriaga 2004. (Note that explicit form
          given in appendix B of the same paper is valid only when l1=l2=l3, as noted in appendix E
          of Lewis, Challinor & Hanson 2011.)
          For the case where only one probe is requested, the below loops reduce to one iteration
          over a single bispectrum (eg. T -> TTT). When two probes are requested, each loop corresponds
          to one of 2^3 bispectra (eg. TTT,TTE,TET,ETT,TEE,ETE,EET,EEE), for a total of 64 elements in
          the covariance matrix. */

          /* Shortcuts to the inverse of the cross-power spectrum */
          double ** C1 = pfi->inverse_C[l1-2];
          double ** C2 = pfi->inverse_C[l2-2];
          double ** C3 = pfi->inverse_C[l3-2];
          
          /* Correction factor for the variance */
          double one_over_delta = 1;
          if ((l1==l2) && (l1==l3))
            one_over_delta = 1/6.;
          else if ((l1==l2) || (l1==l3) || (l2==l3))
            one_over_delta = 0.5;
                    
          /* Compute the Fisher matrix. In the simple case of only one probe (eg. temperature -> TTT),
          each element of the Fisher matrix is simply given by the square of the bispectrum divided
          by three C_l's. When dealing with two probes (eg. T and E -> TTT,TTE,TET,ETT,TEE,ETE,EET,EEE),
          each element of the Fisher matrix, contains a quadratic sum over the 8 available bispectra,
          for a total of 64 terms. */
          for (int X = 0; X < pbi->bf_size; ++X) { for (int A = 0; A < pbi->bf_size; ++A) {
          for (int Y = 0; Y < pbi->bf_size; ++Y) { for (int B = 0; B < pbi->bf_size; ++B) {
          for (int Z = 0; Z < pbi->bf_size; ++Z) { for (int C = 0; C < pbi->bf_size; ++C) {

            double inverse_covariance = C1[A][X] * C2[B][Y] * C3[C][Z];

            for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
              for (int index_ft_2=index_ft_1; index_ft_2 < pfi->fisher_size; ++index_ft_2) {              

                double fisher = one_over_delta *
                                inverse_covariance *
                                interpolated_bispectra[thread][index_ft_1][A][B][C] *
                                interpolated_bispectra[thread][index_ft_2][X][Y][Z];

                /* Fisher matrix as a function of the largest multipole */
                #pragma omp atomic
                pfi->fisher_matrix_XYZ_l1[X][Y][Z][index_l1][index_ft_1][index_ft_2] += fisher;

                /* Fisher matrix as a function of the smallest multipole. Since this quantity is summed
                over the incomplete l1 direction, include the interpolation weight when needed. */
                if (pfi->bispectra_interpolation == mesh_interpolation_2d) {
                  #pragma omp atomic
                  pfi->fisher_matrix_XYZ_l3[X][Y][Z][l3-2][index_ft_1][index_ft_2] += fisher * pwf->delta_l[index_l1];
                }
                else {
                  #pragma omp atomic
                  pfi->fisher_matrix_XYZ_l3[X][Y][Z][l3-2][index_ft_1][index_ft_2] += fisher;
                }
                
                /* Needed to compute the lensing variance. This is equivalent to \bar{F} in Eq. 5.25 
                of http://uk.arxiv.org/abs/1101.2234 */
                if (pfi->include_lensing_effects == _TRUE_) {
                  
                  if (pfi->bispectra_interpolation == mesh_interpolation_2d) {
                    #pragma omp atomic
                    pfi->fisher_matrix_CZ_l3[l3-2][index_ft_1*pbi->bf_size+C][index_ft_2*pbi->bf_size+Z]
                      += fisher * pwf->delta_l[index_l1];
                  }
                  else {
                    #pragma omp atomic
                    pfi->fisher_matrix_CZ_l3[l3-2][index_ft_1*pbi->bf_size+C][index_ft_2*pbi->bf_size+Z]
                      += fisher;
                  }
                }
                
              } // ft_2
            } // ft_1
          }}} // XYZ
          }}} // ABC
                
          #pragma omp atomic
          counter++;          

        } // end of for(l3)
      } // end of for(l2)
    } // end of for(index_l1)

    // ------------------------------------------------------------------------
    // -                              Free memory                            -
    // ------------------------------------------------------------------------
    
    for (int index_ft=0; index_ft < pfi->fisher_size; ++index_ft) {
      for (int X = 0; X < pbi->bf_size; ++X) {
        for (int Y = 0; Y < pbi->bf_size; ++Y) {
          free (interpolated_bispectra[thread][index_ft][X][Y]); }
        free (interpolated_bispectra[thread][index_ft][X]); }
      free (interpolated_bispectra[thread][index_ft]); }
    free (interpolated_bispectra[thread]);
    
  } if (abort == _TRUE_) return _FAILURE_; // end of parallel region
  
  free (interpolated_bispectra);
  
    
  return _SUCCESS_;
  
}




/** 
 * 
 */
int fisher_cross_cls (
        struct precision * ppr,
        struct background * pba,
        struct perturbs * ppt,
        struct bessels * pbs,
        struct transfers * ptr,
        struct primordial * ppm,
        struct spectra * psp,
        struct lensing * ple,
        struct bispectra * pbi,
        struct fisher * pfi
        )
{

  /* The cross-power spectrum is defined as the symmetrix matrix
  C_l^ij = ( C_l^XX C_l^XY )
           ( C_l^YX C_l^YY )
  Its inverse, which we compute below, is needed to compute the covariance matrix
  (see eq. 17 of http://uk.arxiv.org/abs/astro-ph/0701921).
  
  When only one probe is considered (eg. T or E), the cross-power spectrum collapses
  to one number. */
  
  // ==========================================================================
  // =                           Allocate memory                              =
  // ==========================================================================

  /* Allocate memory for the cross-power spectrum and its inverse. We tabulate them
  for all values of l between l_min and l_max, using interpolation. */
  class_alloc (pfi->C, pfi->full_l_size*sizeof(double **), pfi->error_message);
  class_alloc (pfi->inverse_C, pfi->full_l_size*sizeof(double **), pfi->error_message);

  for (int l=pfi->l_min; l <= pfi->l_max; ++l) {

    class_alloc (pfi->C[l-2], pbi->bf_size*sizeof(double*), pfi->error_message);
    class_alloc (pfi->inverse_C[l-2], pbi->bf_size*sizeof(double*), pfi->error_message);

    for (int index_bf=0; index_bf < pbi->bf_size; ++index_bf) {
      class_alloc (pfi->C[l-2][index_bf], pbi->bf_size*sizeof(double), pfi->error_message);
      class_alloc (pfi->inverse_C[l-2][index_bf], pbi->bf_size*sizeof(double), pfi->error_message);
    }
  }

  // ==========================================================================
  // =                          Lensed or not lensed?                         =
  // ==========================================================================
  
  /* The C_l's should be the observed ones, i.e. the LENSED ones (see for example
  Eq. 5.2 of Lewis and Challinor, 2011). */

  double ** cls = pbi->cls;
  int index_cls[pbi->bf_size][pbi->bf_size];
  for (int X=0; X < pbi->bf_size; ++X)
    for (int Y=0; Y < pbi->bf_size; ++Y)
      index_cls[X][Y] = pbi->index_ct_of_bf_bf[X][Y];

  if (pfi->include_lensing_effects == _TRUE_) {
  
    cls = pbi->lensed_cls;
    for (int X=0; X < pbi->bf_size; ++X)
      for (int Y=0; Y < pbi->bf_size; ++Y)
        index_cls[X][Y] = pbi->index_lt_of_bf_bf[X][Y];
  }

  // ==========================================================================
  // =                      Compute cross-power spectrum                      =
  // ==========================================================================


  /* The cross-power spectrum is used to build the covariance matrix and, therefore, needs to be
  include the instrumental noise. We assume that this affects only the direct C_l's,
  eg. C_l^TT and C_l^EE have noise but C_l^TE does not. */
  
  for (int l=pfi->l_min; l <= pfi->l_max; ++l) {  
    for (int X = 0; X < pbi->bf_size; ++X) {
      for (int Y = 0; Y < pbi->bf_size; ++Y) {

        /* Compute ij element */
        pfi->C[l-2][X][Y] = cls[index_cls[X][Y]][l-2];

        /* Add noise only for diagonal combinations (TT, EE). We assume a vanishing noise
        cross-correlation between T and E, as the temperature and polarization detectors
        in a given experiment are usually built independently. */
        if (X==Y) pfi->C[l-2][X][Y] += pfi->N_l[X][l-2];

        /* If a C_l vanishes, then we risk to have infinities in the inverse */
        class_test (fabs(pfi->C[l-2][X][Y]) < _MINUSCULE_, pfi->error_message,
          "C_%d was found ~ zero. Stopping to prevent seg fault. k-max too small?",l);
      }
    }
    
    /* Compute the inverse of C_l^ij for the considered l */
    InverseMatrix (pfi->C[l-2], pbi->bf_size, pfi->inverse_C[l-2]);
    
    /* Debug - print the Cl's */
    // double factor = 1e12*pba->T_cmb*pba->T_cmb*l*(l+1)/(2*_PI_);
    // fprintf (stderr, "%8d %10g %10g %10g %10g\n", l,
    //   factor*pfi->C[l-2][0][0], factor*pfi->C[l-2][1][1],
    //   factor*pfi->C[l-2][0][1], factor*pfi->C[l-2][1][0]);

    /* Debug - print the inverse Cl's */
    // double factor = 1e12*pba->T_cmb*pba->T_cmb*l*(l+1)/(2*_PI_);
    // fprintf (stderr, "%8d %10g %10g %10g %10g\n", l,
    //   pfi->inverse_C[l-2][0][0]/factor, pfi->inverse_C[l-2][1][1]/factor,
    //   pfi->inverse_C[l-2][0][1]/factor, pfi->inverse_C[l-2][1][0]/factor);

  } // end of for(l)
  
  
  return _SUCCESS_;
  
}




/**
 * Add the lensing variance to the Fisher matrix estimator, as explained in Section 5 of
 * Lewis, Challinor & Hanson (2011, http://uk.arxiv.org/abs/1101.2234). This added variance
 * comes from the fact that, in presence of lensing, the assumption of almost Gaussian CMB
 * assumed to obtain the standard form of the covariance matrix fails.
 *
 * In the function we code Eq. 5.35 of Lewis et al., which reduces to Eq. 5.25 in the presence
 * of only one bispectrum type. With respect to that reference we have l1->l3, i->C, j->Z.
 * We also factor out C_l^{X\phi} to avoid inverting singular matrices, as suggested in the
 * paper. Extra credits to Antony also because this function is inspired by the
 * SeparableBispectrum.F90 module of CAMB.
 *
 * This function fills the array pfi->fisher_matrix_lensvar_l3.
 */
int fisher_lensing_variance (
        struct precision * ppr,
        struct background * pba,
        struct perturbs * ppt,
        struct bessels * pbs,
        struct transfers * ptr,
        struct primordial * ppm,
        struct spectra * psp,
        struct lensing * ple,
        struct bispectra * pbi,
        struct fisher * pfi
        )
{

  if (pfi->fisher_verbose > 0)
    printf (" -> adding lensing-induced noise according to arxiv:1101.2234\n");

  /* Dimension of the matrices that we will need to invert. All the arrays will have
  the field (T,E...) and bispectrum types (local template, intrinsic, lensing...) 
  dimensions flattened in a single one. This mixing is explained in the last page
  of Sec. 5 in arxiv:1101.2234  */
  int N = pfi->fisher_size * pbi->bf_size;

  /* Temporary arrays */
  double ** inverse_f_bar, ** temp, ** f;
  class_alloc (inverse_f_bar, N*sizeof(double *), pfi->error_message);
  class_alloc (f, N*sizeof(double *), pfi->error_message);
  for (int X=0; X < N; ++X) {
    class_calloc (inverse_f_bar[X], N, sizeof(double), pfi->error_message);
    class_calloc (f[X], N, sizeof(double), pfi->error_message);
  }    

  /* Start sum over the smallest multipole: l3<=l2<=l1 */
  for (int index_l3=0; index_l3 < pfi->l3_size; ++index_l3) {

    /* By default, assume there is no lensing variance. This instruction is needed
    in order for the 'goto' statements to work */
    for (int i=0; i < N; ++i)
      for (int j=0; j < N; ++j)
        f[i][j] = pfi->fisher_matrix_CZ_l3[index_l3][i][j];
    
    /* Determine current l3 value */
    int l3 = pfi->l3[index_l3];
  
    // ----------------------------------------------------------------------------------
    // -                                Perform checks                                  -
    // ----------------------------------------------------------------------------------
  
    /* Skip l3 contributions where the Fisher matrix has not been computed */
    if ((l3 < pfi->l3_min_global) || (l3 > pfi->l3_max_global))
      goto lensing_variance_final_step;

    /* Skip l3 contributions where the Fisher matrix vanishes. This might happen, for example,
    when l3 is very close to the maximum l, since l3 is the smallest multipole in the sum. */
    int skip = _TRUE_;
    for (int i=0; i < N; ++i)
      if (fabs(pfi->fisher_matrix_CZ_l3[index_l3][i][i]) > _MINUSCULE_)
        skip = _FALSE_;

    if (skip == _TRUE_) {
      if (pfi->fisher_verbose > 1)
        printf ("     * skipping the contribution from l3=%d to the lensing variance (Fisher matrix too small)\n", l3);        
      goto lensing_variance_final_step;
    }
        
    /* Check that the matrix is not singular. If this is the case, pretend there is no lensing
    variance for this value of l3, and jump to the end of the l3 loop, where we rescale the
    Fisher matrix. This can happen if, for example, for this value of (l3,C,Z):
    1) a bispectrum has underflown for certain values of l3 (row of zeros)
    2) a bispectrum has been artificially set to zero for this value of (l3,C,Z) (row of zeros)
    3) two of the bispectra included in the Fisher matrix analysis are too similar, e.g.,
    because you have asked for lensing=yes and for cmb-lensing at the same time (two equal rows) */
    double det = Determinant(pfi->fisher_matrix_CZ_l3[index_l3], N);
    if (fabs(det) < _MINUSCULE_) {

      if (pfi->fisher_verbose > 0) {
        printf ("     * skipping the contribution from l3=%d to the lensing variance (det=%g too small)\n", l3, det);
        printf ("     * F_bar matrix:\n");
        PrintMatrix (pfi->fisher_matrix_CZ_l3[index_l3], N);
      }

      goto lensing_variance_final_step;
    }
    
    /* Uncomment to limit the computation of the lensing variance only to those l's where
    the CMB-lensing bispectrum doesn't vanish. The rationale is that above this scale (
    which is set artificially) the liner C_l^{\phi\phi} is not accurate anymore */
    // int l_max = 0;
    // 
    // if (pbi->has_bispectra_t == _TRUE_)
    //   l_max = MAX (l_max, pbi->lmax_lensing_corrT);
    // if (pbi->has_bispectra_e == _TRUE_)
    //   l_max = MAX (l_max, pbi->lmax_lensing_corrE);
    // 
    // if (l3>l_max) {
    //   
    //   for (int i=0; i < N; ++i)
    //     for (int j=0; j < N; ++j)
    //       f[i][j] = pfi->fisher_matrix_CZ_l3[index_l3][i][j];
    //   
    //   goto lensing_variance_final_step;
    // }
    
    // ------------------------------------------------------------------------------------
    // -                             Compute l3,C,Z contribution                          -
    // ------------------------------------------------------------------------------------
    
    /* Contribution to the considered l3 (smallest multipole in the sum) to the Fisher
    matrix, including lensing variance. This quantity will be summed over C and Z. */
    double l3_contribution = 0;
  
    /* Invert the Fisher matrix with indices Z and C (i and p in Eq. 5.25 of 1101.2234) */
    InverseMatrix (pfi->fisher_matrix_CZ_l3[index_l3], N, inverse_f_bar);
    
    /* Debug - print the matrix and its inverse */
    // printf ("F_bar(l3) for l3=%d:\n", l3);
    // PrintMatrix (pfi->fisher_matrix_CZ_l3[index_l3], N);
    // printf ("F_bar^{-1}(l3) for l3=%d:\n", l3);
    // PrintMatrix (inverse_f_bar, N);
  
    /* Find the lensing-induced noise contribution to the Fisher matrix coming from the considered
    multipole. With respect to Eq. 5.35 of Lewis et al. 2011, this is the term in square brackets.
    It should be noted that there are regions in l-space where the CMB-lensing bispectrum vanishes,
    but the effect of lensing on the Fisher matrix doesn't. In Eq. 5.35, this corresponds to the case
    when the first term in square brackets (C_ZP*C_CP) vanishes while the second one (C_tot_CZ*C_PP)
    doesn't. This might happen for l>100 where C_l^{T\phi} is very small. See also comment to that
    equation in the paper.  */
    for (int C=0; C < pbi->bf_size; ++C) {
      for (int Z=0; Z < pbi->bf_size; ++Z) {
            
        /* Compute the extra noise due to lensing. The cross-correlation between the two fields
        (C_tot_CZ) should include the instrument noise, as specified below Eq. 5.1 of Lewis
        et al. 2011. The second term (C_tot_CZ*C_PP) dominates over the first one (C_ZP*C_CP).
        Note that the noise_correction with respect to Eq. 5.35 (ibidem) is rescaled by a factor
        C_ZP*C_CP in order to avoid numerical issues (we cannot trust neither C_ZP nor C_CP
        for l>~100) */
        double C_tot_CZ = pfi->C[l3-2][C][Z];
        double C_CP = pbi->cls[pbi->index_ct_of_phi_bf[ C ]][l3-2];
        double C_ZP = pbi->cls[pbi->index_ct_of_phi_bf[ Z ]][l3-2];
        double C_PP = pbi->cls[psp->index_ct_pp][l3-2];
        double noise_correction = (C_ZP*C_CP + C_tot_CZ*C_PP)/(2*l3+1);
        // double noise_correction = (1 + C_tot_CZ*C_PP/(C_ZP*C_CP))/(2*l3+1);
  
        /* Contribution to the inverse Fisher matrix from this (l1,Z,C) */
        int index_C = pfi->index_ft_cmb_lensing_kernel*pbi->bf_size+C;
        int index_Z = pfi->index_ft_cmb_lensing_kernel*pbi->bf_size+Z;
        inverse_f_bar[index_C][index_Z] += noise_correction;

        /* Debug - print out the correction to F_bar(l3,C,Z)^-1. To plot the file in gnuplot, do:
        set term wxt 1; set log; plot [2:100] "noise_correction.dat" u 1:(abs($5)) every\
        4::0 w li t "noise TT", "" u 1:(abs($5)) every 4::1 w li t "noise TE", "" u 1:(abs($5))\
        every 4::3 w li t "noise EE” */
        // if (l3<200)
        //   fprintf (stderr, "%4d %4s %4s %17g %17g %17g %17g %17g %17g %17g\n",
        //     l3, pbi->bf_labels[C], pbi->bf_labels[Z],
        //     noise_correction,
        //     C_ZP*C_CP/(2*l3+1),
        //     C_tot_CZ*C_PP/(2*l3+1),
        //     inverse_f_bar[index_C][index_Z],
        //     (pbi->has_intrinsic==_TRUE_)?
        //     C_ZP*C_CP*inverse_f_bar[pfi->index_ft_intrinsic*pbi->bf_size+C]
        //     [pfi->index_ft_intrinsic*pbi->bf_size+Z]:0,
        //     (pbi->has_intrinsic_squeezed==_TRUE_)?
        //     C_ZP*C_CP*inverse_f_bar[pfi->index_ft_intrinsic_squeezed*pbi->bf_size+C]
        //     [pfi->index_ft_intrinsic_squeezed*pbi->bf_size+Z]:0,
        //     (pbi->has_local_model==_TRUE_)?
        //     C_ZP*C_CP*inverse_f_bar[pfi->index_ft_local*pbi->bf_size+C]
        //     [pfi->index_ft_local*pbi->bf_size+Z]:0
        //   );

        /* Debug - print r_l. This should match Fig. 3 of Lewis et al. 2011 when considering only
        temperature or only polarisation. Note that the r-plot thus produced will look as the
        absolute value of the curves in Fig. 3, as we compute it as 1/sqrt(r^-2). */
        // double r_minus_2 = C_tot_CZ*C_PP/(C_ZP*C_CP);
        // double r = 1/sqrt(r_minus_2);
        // double l_factor = l3*(l3+1)/(2*_PI_);
        // double t_factor = 2.7255*1e6;
        // double factor = pow(t_factor,2) * l_factor;
        // fprintf (stderr, "%4d %17g %17g %17g %17g %17g\n",
        //   l3, r, factor*C_tot_CZ, l_factor*l3*(l3+1)*C_PP,
        //   t_factor*l_factor*sqrt(l3*(l3+1))*C_ZP, t_factor*l_factor*sqrt(l3*(l3+1))*C_CP);
  
      } // Z
    } // C
    
    /* Debug - print the inverse matrix after adding the noise correction */
    // printf ("F^{-1}(l3) for l3=%d:\n", l3);
    // PrintMatrix (inverse_f_bar, N);
    
    /* Check that the matrix is not singular */
    det = Determinant(inverse_f_bar, N);
    if (fabs(det) < _MINUSCULE_) {

      if (pfi->fisher_verbose > 0) {
        printf ("     * skipping the contribution from l3=%d to the lensing variance (det=%g too small)\n", l3, det);
        printf ("     * F_bar^{-1} + noise matrix:\n");
        PrintMatrix (pfi->fisher_matrix_CZ_l3[index_l3], N);
      }

      goto lensing_variance_final_step;
    }

    
    /* Invert the contribution from (l1,Z,C) */
    InverseMatrix (inverse_f_bar, N, f);

    /* Check that the inversion didn't produce nans */
    for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1)
      for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2)
        for (int C=0; C < pbi->bf_size; ++C)
          for (int Z=0; Z < pbi->bf_size; ++Z)
            class_test (isnan (f[index_ft_1*pbi->bf_size+C][index_ft_2*pbi->bf_size+Z]),
              pfi->error_message,
              "stopping to prevent nans");

    
    // ---------------------------------------------------------------------------------------------
    // -                                   Add up the contributions                                -
    // ---------------------------------------------------------------------------------------------

    /* Sum over (l3,Z,C) to obtain the optimal Fisher matrix */
    lensing_variance_final_step:

    for (int index_ft_1=0; index_ft_1 < pfi->fisher_size; ++index_ft_1) {
      for (int index_ft_2=0; index_ft_2 < pfi->fisher_size; ++index_ft_2) {
      
        for (int C=0; C < pbi->bf_size; ++C) {
          for (int Z=0; Z < pbi->bf_size; ++Z) {

            /* So far we have been using the kernel of the CMB-lensing bispectrum (Eq. 5.20 of
            arXiv:1101.2234). The reason for doing so is to avoid numerical noise in the term
            C_l1^{i\psi}*C_l1^{j\psi} at the denominator of Eq. 5.35. Here we rescale the Fisher
            matrix so that it contains the actual CMB-lensing bispectrum by multiplying it
            by C_l1^{i\psi}*C_l1^{j\psi}. */
            double correction = 1;
            if (index_ft_1 == pfi->index_ft_cmb_lensing_kernel)
              correction *= pbi->cls[pbi->index_ct_of_phi_bf[ C ]][l3-2];
            if (index_ft_2 == pfi->index_ft_cmb_lensing_kernel)
              correction *= pbi->cls[pbi->index_ct_of_phi_bf[ Z ]][l3-2];
        
            pfi->fisher_matrix_lensvar_l3[index_l3][index_ft_1][index_ft_2]
              += correction * f[index_ft_1*pbi->bf_size+C][index_ft_2*pbi->bf_size+Z];
    
            /* Rescale also the zero-signal matrix */
            pfi->fisher_matrix_CZ_l3[index_l3][index_ft_1*pbi->bf_size+C][index_ft_2*pbi->bf_size+Z]
              *= correction;
            
          } // Z
        } // C
      } // ft_2
    } // ft_1
    
  } // index_l3
  
  /* Free memory */
  for (int X=0; X < N; ++X) {
    free (inverse_f_bar[X]);
    free (f[X]);
  }
  free (inverse_f_bar);
  free (f);
  
  return _SUCCESS_;
  
}



