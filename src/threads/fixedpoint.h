/* 17.14 fixed point
   f = 2**14 */
#define f 16384

/* Convert n to fixed point */
#define convert_to_fixedpoint(n) (n*f)

/* Convert x to integer (rounding toward zero) */
#define convert_to_int_round_zero(x) (x/f)

/* Convert x to integer (rounding to nearest) */
#define convert_to_int_round_nearest(x) (x >= 0 ? (x + f/2)/f  : (x - f/2)/f)

/* Add two fixed points */
#define add(x,y) (x+y)

/* Subtract two fixed points */
#define subtract(x,y) (x-y)

/* Add a fixed point with an integer */
#define addn(x,n) (x+(n*f))

/* Subtract a fixed point with an integer */
#define subtractn(x,n) (x-(n*f))

/* Multiply two fixed points */
#define multiply(x,y) (((int64_t) x ) * y / f)

/* Multiply one fixed point with an integer */
#define multiplyn(x,n) (x * n)

/* Divide two fixed points */
#define divide(x,y) (((int64_t) x ) * f / y)

/* Divide one fixed point with an integer */
#define dividen(x,n) (x / n)
