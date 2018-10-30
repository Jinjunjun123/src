/* One Dimensional Dynamic Time Warping underlying programs*/
#include <rsf.h>

/* reverses the second axis of an array */
void dtw_reverse_array(float* array, float* reverse, int n2, int n1){
	for (int i = 0 ; i < n2 ; i++){
		for (int j = 0 ; j < n1 ; j++){
			reverse[(n2-i-1)*n1 + j] = array[i*n1 + j] ;
		}
	}
}
/* adds two arrays */
void dtw_two_sided_smoothing_sum( float* ef, float* er, float* e, float* etilde, int n){
	for (int i = 0 ; i < n ; i ++){
		if (ef[i] < 0 || er[i] < 0 || e[i] < 0) etilde[i] = -1;
		else etilde[i] = ef[i] + er[i] - e[i];
	}
}
/* finds pointwise alignment errors for shifts between min and max */
void dtw_alignment_errors( float* match, float* ref, float* mismatch, int n1, int maxshift , float ex){
	int j2;
	/* loop through samples in reference trace*/
	for (int i = 0 ; i < n1 ; i++){
		/* loop through permissible shifts */
		for (int j = 0 ; j < 2 * maxshift + 1 ; j++){
			j2 = i + ( j - maxshift );
			/* if in bounds write difference */
			if (j2 >= 0 && j2 < n1){ 
			    mismatch[ i*(2*maxshift+1) + j] = pow(fabsf(ref[ i] - match[ j2]),ex);
			}
			/* otherwise write a null value */
			else { mismatch [ i*(2*maxshift+1) + j] = -1.;}
		}
	}
	return;
}
/* finds the minimum non-null value if one exists */
float dtw_posmin(float a, float b){
	if( a >=0 && b >= 0){
		if ( a < b) return a;
		else return b;
	} else {
		if (a >= 0 ){
			return a;
		} else {
			return b;
		}
	}
}
/* sums up bounded strains */
float dtw_strain_accumulator(float* accumulate, float* mismatch, int i, int l, int s, int maxshift){
    /* initialize */
    float accum ;
	if ( i-s > 0) accum	= accumulate[ (i-s)*(2*maxshift+1) + l];
	else accum = 0;
	for (int j = 0 ; j < s ; j++){
		if ( mismatch[(i-j)*(2*maxshift+1) + l] < 0) return mismatch[(i-j)*(2*maxshift+1) + l];
		accum += mismatch[(i-j)*(2*maxshift+1) + l];
	}
	return accum;
}
/* finds the minimum of 3 errors, checks for null values */
float dtw_acumin( float* accumulate, float* mismatch, int i, int l, int n1, int maxshift, int sb){
	/* read in the comparison values if in bounds */
	float first ;
	if (l-1 >=0){
	//    first = accumulate[ (i-1)*(2*maxshift+1) + l - 1];
        first = dtw_strain_accumulator(accumulate, mismatch, i, l-1, sb, maxshift);
	} else { 
		first = -1 ;
	}
	float second = accumulate[ (i-1)*(2*maxshift+1)+l];
	float third;
	if (l+1 < 2*maxshift+1 ){
	    third = dtw_strain_accumulator(accumulate, mismatch, i, l+1, sb, maxshift) ;
	} else { 
		third = -1 ;
	}	
	/* find the smallest non-null value */
	return dtw_posmin( first, dtw_posmin( second, third)) ;
}
/* backtrack to determine the shifts */
void dtw_backtrack( float* accumulate, float* mismatch, int* shifts, int n1, int maxshift, float str){
	int sb = (int)(1/str);
	/* determine initial shifts */
	int  u = 0;
	int du = 0;
	float holder = 0;
	float test;
	for (int l = 1; l < 2*maxshift+1 ; l++){
		if ( accumulate [ (n1-1)*(2*maxshift+1) + l] < 0) continue;
		if ( accumulate [ (n1-1)*(2*maxshift+1) + l] < accumulate [ (n1-1)*(2*maxshift+1) + u]){
			u = l;
		}
	}
	/* write initial shift */
	shifts [ n1-1] = u - maxshift;	
	/* do initial shifts until we attain our strian bound */
	for (int s = 1 ; s < sb ; s++){
		u = shifts [n1-s];
		du = 0;
		holder = accumulate[ (n1-s-1)*(2*maxshift+1) + u + maxshift];
		if (u-1 + maxshift >= 0){
			test = dtw_strain_accumulator(accumulate, mismatch, n1-s-1, u-1 + maxshift, s, maxshift);
			if( test < holder ){
				holder = test;
				du = -1;
			}
		}
		if (u+1 + maxshift < 2*maxshift+1){
			test = dtw_strain_accumulator(accumulate, mismatch, n1-s-1, u+1 + maxshift, s, maxshift);
			if( test < holder ){
				holder = test;
				du = 1;
			}
		}
		shifts[ n1-s-1] = u+du;
	}
	/* loop backward through remaining time samples */
	for (int i = n1-sb-1 ; i >= 0 ; i--){
		u  = shifts[ i + 1 ];
		du = 0;
		holder = accumulate[ i*(2*maxshift+1) + u + maxshift ] ;
	    /* test above */
		if ( u-1 + maxshift >= 0 ){
			test = dtw_strain_accumulator(accumulate, mismatch, i, u-1 + maxshift, sb, maxshift);
			if (test < holder){
				holder = test;
				du = - 1;
			}
		}
		/* test below */
		if ( u+1 + maxshift < 2*maxshift+1 ){
			test = dtw_strain_accumulator(accumulate, mismatch, i, u+1 + maxshift, sb, maxshift);
			if (test < holder){
				du = 1;
			}
		}		
		/* write minimizing shift */
		shifts [ i] = u + du;
	}
	
}
/* spread the accumulation back over null values */
void dtw_spread_over_nulls( float* accumulate, int n1, int maxshift){
	for ( int i = 0; i < maxshift ; i++){
		for (int l = 0 ; l < maxshift - i; l++){
			/* beginning of signal */
			accumulate[i*(2*maxshift+1)+l] = 
				accumulate[(maxshift-l)*(2*maxshift+1)+l] ;
			/* end of signal */
			accumulate[(n1-i-1)*(2*maxshift+1)+(2*maxshift-l)] = 
				accumulate[(n1-1-maxshift+l)*(2*maxshift+1)+(2*maxshift-l)] ;
		}
	}
}
/* accumulates errors over trajectories */
void dtw_accumulate_errors( float* mismatch, float* accumulate, int n1, int maxshift, float str){
    /* strain bound */
	int sb = (int)(1/str);
	/* initialize with first step */
	for (int s = 0 ; s < sb ; s++){
		for (int l = 0 ; l < 2*maxshift+1 ; l++){
			accumulate[s*(2*maxshift+1) + l] = dtw_strain_accumulator(accumulate, mismatch, s, l, s, maxshift);
		}
	}
	/* loop through rest of time indeces*/
	for (int i = sb ; i < n1 ; i++){
		/* loop through lags */
		for (int l = 0 ; l < 2*maxshift+1 ; l++){
			/* check to see if index makes sense, negative means null */
			if ( mismatch[ i*(2*maxshift+1) + l] < 0){
				accumulate[ i*(2*maxshift+1) + l ] = -1;
				continue;
			}
			/* Hale's equation 6 */
			float minval = dtw_acumin( accumulate, mismatch, i, l, n1, maxshift, sb);
			if (minval >= 0 ){
			    accumulate[ i*(2*maxshift+1) + l] = mismatch[ i*(2*maxshift+1) + l] + minval;
		    } else {
				/* write null val if all negative */ 
				accumulate[ i*(2*maxshift+1) + l] = minval ; 
			}
		}
	}
	return;
}
/* apply known integer shifts to matching trace */
void dtw_apply_shifts( float* match, int* shifts, float* warped, int n){
	for (int i = 0 ; i < n ; i++){
		/* make sure shifts in bounds */
		if (i + shifts[ i] < 0){
			warped [ i] = match [ 0];
		} else { 
			if (i + shifts[ i] > n-1){
				warped[ i] = match [ n-1] ;
			} else{
				/* apply shifts */
				warped [ i] = match[ i + shifts[ i]]; 
			}
			
		}
	}
	return;
}
/* set lag pannel parameters  */
void dtw_set_file_params(sf_file _file, int n1, float d1, float o1,
    const char* label1, const char* unit1,
    int n2, float d2, float o2,
	const char* label2, const char* unit2){
	/* set output parameters */
	sf_putint(_file,"n1",n1);
	sf_putfloat(_file,"d1",d1);
	sf_putfloat(_file,"o1",o1);
    sf_putstring(_file,"label1", label1);
	sf_putstring(_file, "unit1",  unit1);
	sf_putint(_file,"n2",n2);
	sf_putfloat(_file,"d2",d2 );
	sf_putfloat(_file,"o2", o2);
    sf_putstring(_file,"label2", label2);
	sf_putstring(_file,"unit2", unit2);
	return;
}