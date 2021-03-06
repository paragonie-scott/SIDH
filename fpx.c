/********************************************************************************************
* SIDH: an efficient supersingular isogeny-based cryptography library for Diffie-Hellman key 
*       exchange providing 128 bits of quantum security and 192 bits of classical security.
*
*    Copyright (c) Microsoft Corporation. All rights reserved.
*
*
* Abstract: core functions over GF(p751^2) and field operations over the prime p751
*
*********************************************************************************************/ 

#include "SIDH_internal.h"
    

// Global constants          
const uint64_t p751[NWORDS_FIELD]          = { 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xEEAFFFFFFFFFFFFF,
                                               0xE3EC968549F878A8, 0xDA959B1A13F7CC76, 0x084E9867D6EBE876, 0x8562B5045CB25748, 0x0E12909F97BADC66, 0x00006FE5D541F71C };
const uint64_t p751p1[NWORDS_FIELD]        = { 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0xEEB0000000000000,
                                               0xE3EC968549F878A8, 0xDA959B1A13F7CC76, 0x084E9867D6EBE876, 0x8562B5045CB25748, 0x0E12909F97BADC66, 0x00006FE5D541F71C };
const uint64_t Montgomery_R2[NWORDS_FIELD] = { 0x233046449DAD4058, 0xDB010161A696452A, 0x5E36941472E3FD8E, 0xF40BFE2082A2E706, 0x4932CCA8904F8751 ,0x1F735F1F1EE7FC81, 
                                               0xA24F4D80C1048E18, 0xB56C383CCDB607C5, 0x441DD47B735F9C90, 0x5673ED2C6A6AC82A, 0x06C905261132294B, 0x000041AD830F1F35 }; 


/*******************************************************/
/************* Field arithmetic functions **************/

__inline void fpcopy751(felm_t a, felm_t c)
{ // Copy of a field element, c = a
    unsigned int i;

    for (i = 0; i < NWORDS_FIELD; i++) {
        c[i] = a[i];
    }
}


__inline void fpzero751(felm_t a)
{ // Zeroing a field element, a = 0
    unsigned int i;

    for (i = 0; i < NWORDS_FIELD; i++) {
        a[i] = 0;
    }
}


void to_mont(felm_t a, felm_t mc)
{ // Conversion to Montgomery representation
  // mc = a*R^2*R^-1 mod p751 = a*R mod p751, where a in [0, p751-1]
  // The Montgomery constant R^2 mod p751 is the global value "Montgomery_R2". 

    fpmul751_mont(
        a, 
        (digit_t*) &Montgomery_R2,
        mc
    );
}


void from_mont(felm_t ma, felm_t c)
{ // Conversion from Montgomery representation to standard representation
  // c = ma*R^-1 mod p751 = a mod p751, where ma in [0, p751-1].
    digit_t one[NWORDS_FIELD] = {0};
    
    one[0] = 1;
    fpmul751_mont(ma, one, c);
}


static __inline unsigned int is_felm_zero(felm_t x)
{ // Is x = 0? return 1 (TRUE) if condition is true, 0 (FALSE) otherwise
    unsigned int i;
    unsigned int r = 0;

    for (i = 0; i < NWORDS_FIELD; i++) {
        r |= x[i] ^ 0;
    }
    return r;
}


static __inline unsigned int is_felm_even(felm_t x)
{ // Is x even? return 1 (TRUE) if condition is true, 0 (FALSE) otherwise
    return (unsigned int)((x[0] & 1) ^ 1);
}


static __inline unsigned int is_felm_lt(felm_t x, felm_t y)
{ // Is x < y? return 1 (TRUE) if condition is true, 0 (FALSE) otherwise
    int i;
    int r = 0;

    for (i = NWORDS_FIELD - 1; i >= 0; i--) {
        r += CT_LT(x[i], y[i]) & 1;
    }
    return r;
}


void copy_words(digit_t* a, digit_t* c, unsigned int nwords)
{ // Copy wordsize digits, c = a, where lng(a) = nwords
    unsigned int i;
        
    for (i = 0; i < nwords; i++) {                      
        c[i] = a[i];
    }
}


__inline unsigned int mp_sub(digit_t* a, digit_t* b, digit_t* c, unsigned int nwords)
{ // Multiprecision subtraction, c = a-b, where lng(a) = lng(b) = nwords. Returns the borrow bit 
    unsigned int i, borrow = 0;

    for (i = 0; i < nwords; i++) {
        SUBC(borrow, a[i], b[i], borrow, c[i]);
    }

    return borrow;
}


__inline unsigned int mp_add(digit_t* a, digit_t* b, digit_t* c, unsigned int nwords)
{ // Multiprecision addition, c = a+b, where lng(a) = lng(b) = nwords. Returns the carry bit 
    unsigned int i, carry = 0;
        
    for (i = 0; i < nwords; i++) {                      
        ADDC(carry, a[i], b[i], carry, c[i]);
    }

    return carry;
}


void mp_shiftr1(digit_t* x, unsigned int nwords)
{ // Multiprecision right shift by one
    unsigned int i;

    for (i = 0; i < nwords - 1; i++) {
        SHIFTR(
            x[i + 1],
            x[i],
            1,
            x[i],
            RADIX
        );
    }
    x[nwords - 1] >>= 1;
}


void mp_shiftl1(digit_t* x, unsigned int nwords)
{ // Multiprecision left right shift by one
    int i;

    for (i = nwords - 1; i > 0; i--) {
        SHIFTL(
            x[i],
            x[i - 1],
            1,
            x[i],
            RADIX
        );
    }
    x[0] <<= 1;
}


static __inline void power2_setup(felm_t x, int mark)
{  // Set up the value 2^mark
    unsigned int i = 0;
    
    fpzero751(x);
    while (mark >= 0) {
        if (mark < RADIX) {
            x[i] = (digit_t) 1 << mark;
        }
        mark -= RADIX;
        i += 1;
    }
}


void fpmul751_mont(felm_t ma, felm_t mb, felm_t mc)
{ // 751-bit Comba multi-precision multiplication, c = a*b mod p751
    dfelm_t temp = {0};

    mp_mul(ma, mb, temp, NWORDS_FIELD);
    rdc_mont(temp, mc);
}


void fpsqr751_mont(felm_t ma, felm_t mc)
{ // 751-bit Comba multi-precision squaring, c = a^2 mod p751
    dfelm_t temp = {0};

    mp_mul(ma, ma, temp, NWORDS_FIELD);
    rdc_mont(temp, mc);
}


void fpinv751_mont(felm_t a)
{// Field inversion using Montgomery arithmetic, a = a^-1*R mod p751
    felm_t t[27], tt;
    unsigned int i, j;
    
    // Precomputed table
    fpsqr751_mont(a, tt);
    fpmul751_mont(a, tt, t[0]);
    fpmul751_mont(t[0], tt, t[1]);
    fpmul751_mont(t[1], tt, t[2]);
    fpmul751_mont(t[2], tt, t[3]); 
    fpmul751_mont(t[3], tt, t[3]);
    for (i = 3; i <= 8; i++) {
        fpmul751_mont(t[i], tt, t[i+1]);
    }
    fpmul751_mont(t[9], tt, t[9]);
    for (i = 9; i <= 20; i++) {
        fpmul751_mont(t[i], tt, t[i+1]);
    }
    fpmul751_mont(t[21], tt, t[21]); 
    for (i = 21; i <= 24; i++) {
        fpmul751_mont(t[i], tt, t[i+1]);
    }
    fpmul751_mont(t[25], tt, t[25]);
    fpmul751_mont(t[25], tt, t[26]);

    fpcopy751(a, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[20], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[24], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[11], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[8], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[2], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[23], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[2], tt, tt);
    for (i = 0; i < 9; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[2], tt, tt);
    for (i = 0; i < 10; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[15], tt, tt);
    for (i = 0; i < 8; i++){
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[13], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[26], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[20], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[11], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[10], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[14], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[4], tt, tt);
    for (i = 0; i < 10; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[18], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[1], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[22], tt, tt);
    for (i = 0; i < 10; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[6], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[24], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[9], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[18], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[17], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(a, tt, tt);
    for (i = 0; i < 10; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[16], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[7], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[0], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[12], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[19], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[22], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[25], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[2], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[10], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[22], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[18], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[4], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[14], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[13], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[5], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[23], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[21], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[2], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[23], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[12], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[9], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[3], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[13], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[17], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[26], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[5], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[8], tt, tt);
    for (i = 0; i < 8; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[11], tt, tt);
    for (i = 0; i < 6; i++) {
        fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[22], tt, tt);
    for (i = 0; i < 7; i++) {
        fpsqr751_mont(tt, tt);
    }
    
    for (j = 0; j < 61; j++) {
        fpmul751_mont(t[26], tt, tt);
        for (i = 0; i < 6; i++) fpsqr751_mont(tt, tt);
    }
    fpmul751_mont(t[25], tt, a);  
}


/***********************************************/
/************* GF(p^2) FUNCTIONS ***************/

void fp2copy751(f2elm_t a, f2elm_t c)
{// Copy of a GF(p751^2) element, c = a
    fpcopy751(a[0], c[0]);
    fpcopy751(a[1], c[1]);
}


void fp2zero751(f2elm_t a)
{// Zeroing a GF(p751^2) element, a = 0
    fpzero751(a[0]);
    fpzero751(a[1]);
}


void fp2neg751(f2elm_t a)
{// GF(p751^2) negation, a = -a in GF(p751^2)
    fpneg751(a[0]);
    fpneg751(a[1]);
}


__inline void fp2add751(f2elm_t a, f2elm_t b, f2elm_t c)           
{// GF(p751^2) addition, c = a+b in GF(p751^2)
    fpadd751(a[0], b[0], c[0]);
    fpadd751(a[1], b[1], c[1]);
}


__inline void fp2sub751(f2elm_t a, f2elm_t b, f2elm_t c)          
{// GF(p751^2) subtraction, c = a-b in GF(p751^2) 
    fpsub751(a[0], b[0], c[0]);
    fpsub751(a[1], b[1], c[1]);
}


void fp2div2_751(f2elm_t a, f2elm_t c)          
{// GF(p751^2) division by two, c = a/2  in GF(p751^2) 
    fpdiv2_751(a[0], c[0]);
    fpdiv2_751(a[1], c[1]);
}


void fp2sqr751_mont(f2elm_t a, f2elm_t c)
{// GF(p751^2) squaring using Montgomery arithmetic, c = a^2 in GF(p751^2)
    felm_t t1, t2, t3;

    mp_add(a[0], a[1], t1, NWORDS_FIELD);    // t1 = a0+a1 
    fpsub751(a[0], a[1], t2);                // t2 = a0-a1
    mp_add(a[0], a[0], t3, NWORDS_FIELD);    // t3 = 2a0
    fpmul751_mont(t1, t2, c[0]);             // c0 = (a0+a1)(a0-a1)
    fpmul751_mont(t3, a[1], c[1]);           // c1 = 2a0*a1
}


void fp2mul751_mont(f2elm_t a, f2elm_t b, f2elm_t c)
{// GF(p751^2) multiplication using Montgomery arithmetic, c = a*b in GF(p751^2)
    felm_t t1, t2;
    dfelm_t tt1, tt2, tt3; 
    digit_t mask;
    unsigned int i, borrow;

    mp_mul(a[0], b[0], tt1, NWORDS_FIELD);            // tt1 = a0*b0
    mp_mul(a[1], b[1], tt2, NWORDS_FIELD);            // tt2 = a1*b1
    mp_add(a[0], a[1], t1, NWORDS_FIELD);             // t1 = a0+a1
    mp_add(b[0], b[1], t2, NWORDS_FIELD);             // t2 = b0+b1
    borrow = mp_sub(tt1, tt2, tt3, 2 * NWORDS_FIELD); // tt3 = a0*b0 - a1*b1
    mask = 0 - (digit_t) borrow;                      // if tt3 < 0 then mask = 0xFF..F, else if tt3 >= 0 then mask = 0x00..0
    borrow = 0;
    for (i = 0; i < NWORDS_FIELD; i++) {             // tt3 = tt3 + (mask & 2^768*p751)
        ADDC(
            borrow,
            tt3[NWORDS_FIELD + i],
            ((digit_t*) p751)[i] & mask,
            borrow,
            tt3[NWORDS_FIELD+i]
        );
    }
    rdc_mont(tt3, c[0]);                             // c[0] = a0*b0 - a1*b1
    mp_add(tt1, tt2, tt1, 2 * NWORDS_FIELD);         // tt1 = a0*b0 + a1*b1
    mp_mul(t1, t2, tt2, NWORDS_FIELD);               // tt2 = (a0+a1)*(b0+b1)
    mp_sub(tt2, tt1, tt2, 2 * NWORDS_FIELD);         // tt2 = (a0+a1)*(b0+b1) - a0*b0 - a1*b1 
    rdc_mont(tt2, c[1]);                             // c[1] = (a0+a1)*(b0+b1) - a0*b0 - a1*b1 
}


void to_fp2mont(f2elm_t a, f2elm_t mc)
{ // Conversion of a GF(p751^2) element to Montgomery representation
  // mc_i = a_i*R^2*R^-1 = a_i*R in GF(p751^2). 

    to_mont(a[0], mc[0]);
    to_mont(a[1], mc[1]);
}


void from_fp2mont(f2elm_t ma, f2elm_t c)
{ // Conversion of a GF(p751^2) element from Montgomery representation to standard representation
  // c_i = ma_i*R^-1 = a_i in GF(p751^2).

    from_mont(ma[0], c[0]);
    from_mont(ma[1], c[1]);
}


void fp2inv751_mont(f2elm_t a)
{// GF(p751^2) inversion using Montgomery arithmetic, a = (a0-i*a1)/(a0^2+a1^2)
    f2elm_t t1;

    fpsqr751_mont(a[0], t1[0]);             // t10 = a0^2
    fpsqr751_mont(a[1], t1[1]);             // t11 = a1^2
    fpadd751(t1[0], t1[1], t1[0]);          // t10 = a0^2+a1^2
    fpinv751_mont(t1[0]);                   // t10 = (a0^2+a1^2)^-1
    fpneg751(a[1]);                         // a = a0-i*a1
    fpmul751_mont(a[0], t1[0], a[0]);
    fpmul751_mont(a[1], t1[0], a[1]);       // a = (a0-i*a1)*(a0^2+a1^2)^-1
}


void swap_points_basefield(
    point_basefield_proj_t P,
    point_basefield_proj_t Q,
    digit_t option
) { // Swap points over the base field 
  // If option = 0 then P <- P and Q <- Q, else if option = 0xFF...FF then P <- Q and Q <- P
    digit_t temp;
    unsigned int i;

    for (i = 0; i < NWORDS_FIELD; i++) {
        temp = option & (P->X[i] ^ Q->X[i]);
        P->X[i] = temp ^ P->X[i]; 
        Q->X[i] = temp ^ Q->X[i]; 
        temp = option & (P->Z[i] ^ Q->Z[i]);
        P->Z[i] = temp ^ P->Z[i]; 
        Q->Z[i] = temp ^ Q->Z[i]; 
    }
}


void swap_points(point_proj_t P, point_proj_t Q, digit_t option)
{ // Swap points
  // If option = 0 then P <- P and Q <- Q, else if option = 0xFF...FF then P <- Q and Q <- P
    digit_t temp;
    unsigned int i;

    for (i = 0; i < NWORDS_FIELD; i++) {
        temp = option & (P->X[0][i] ^ Q->X[0][i]);
        P->X[0][i] = temp ^ P->X[0][i]; 
        Q->X[0][i] = temp ^ Q->X[0][i]; 
        temp = option & (P->Z[0][i] ^ Q->Z[0][i]);
        P->Z[0][i] = temp ^ P->Z[0][i]; 
        Q->Z[0][i] = temp ^ Q->Z[0][i]; 
        temp = option & (P->X[1][i] ^ Q->X[1][i]);
        P->X[1][i] = temp ^ P->X[1][i]; 
        Q->X[1][i] = temp ^ Q->X[1][i]; 
        temp = option & (P->Z[1][i] ^ Q->Z[1][i]);
        P->Z[1][i] = temp ^ P->Z[1][i]; 
        Q->Z[1][i] = temp ^ Q->Z[1][i]; 
    }
}


void select_f2elm(f2elm_t x, f2elm_t y, f2elm_t z, digit_t option)
{ // Select either x or y depending on value of option 
  // If option = 0 then z <- x, else if option = 0xFF...FF then z <- y
    unsigned int i;

    for (i = 0; i < NWORDS_FIELD; i++) {
        z[0][i] = (option & (x[0][i] ^ y[0][i])) ^ x[0][i]; 
        z[1][i] = (option & (x[1][i] ^ y[1][i])) ^ x[1][i]; 
    }
}