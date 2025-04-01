#include <jlib/sort.h>

static void _jx_quickSort(void* base, uint64_t totalElems, uint64_t elemSize, jsortComparisonCallback cmp, void* userData);

jx_sort_api* sort_api = &(jx_sort_api) {
	.quickSort = _jx_quickSort
};

//////////////////////////////////////////////////////////////////////////
// Quick Sort
//
// https://github.com/coreutils/gnulib/blob/master/lib/qsort.c
//

// Discontinue quicksort algorithm when partition gets below this size.
#define JSORT_CONFIG_QUICKSORT_MAX_THRESH 4

// The next 4 #defines implement a very fast in-line stack abstraction.
// The stack needs log (total_elements) entries (we could even subtract
// log(MAX_THRESH)).  Since total_elements has type size_t, we get as
// upper bound for log (total_elements):
// bits per byte (CHAR_BIT) * sizeof(size_t).
#define STACK_SIZE (8 * sizeof(uint64_t))
#define PUSH(low, high)	((void) ((top->lo = (low)), (top->hi = (high)), ++top))
#define	POP(low, high)	((void) (--top, (low = top->lo), (high = top->hi)))
#define	STACK_NOT_EMPTY	(stack < top)

// Byte-wise swap two items of size SIZE.
#define SWAP(a, b, size) \
    do                                  \
    {                                   \
        uint64_t __size = (size);       \
        uint8_t *__a = (a), *__b = (b); \
        do                              \
        {                               \
            uint8_t __tmp = *__a;       \
            *__a++ = *__b;              \
            *__b++ = __tmp;             \
        } while (--__size > 0);         \
    } while (0)

// Stack node declarations used to store unfulfilled partition obligations.
typedef struct jsort_qsort_stack_node
{
    uint8_t* lo;
    uint8_t* hi;
} jsort_qsort_stack_node;

static void _jx_quickSort(void* base, uint64_t totalElems, uint64_t elemSize, jsortComparisonCallback cmp, void* userData)
{
    uint8_t* base_ptr = (uint8_t*)base;

    const uint64_t max_thresh = JSORT_CONFIG_QUICKSORT_MAX_THRESH * elemSize;

    if (totalElems == 0) {
        /* Avoid lossage with unsigned arithmetic below.  */
        return;
    }

    if (totalElems > JSORT_CONFIG_QUICKSORT_MAX_THRESH) {
        uint8_t* lo = base_ptr;
        uint8_t* hi = &lo[elemSize * (totalElems - 1)];
        jsort_qsort_stack_node stack[STACK_SIZE];
        jsort_qsort_stack_node* top = stack;

        PUSH(NULL, NULL);

        while (STACK_NOT_EMPTY) {
            uint8_t* left_ptr;
            uint8_t* right_ptr;

            // Select median value from among LO, MID, and HI. Rearrange
            // LO and HI so the three values are sorted. This lowers the
            // probability of picking a pathological pivot value and
            // skips a comparison for both the LEFT_PTR and RIGHT_PTR in
            // the while loops.
            uint8_t* mid = lo + elemSize * ((hi - lo) / elemSize >> 1);

            if (cmp((void*)mid, (void*)lo, userData) < 0) {
                SWAP(mid, lo, elemSize);
            }

            if (cmp((void*)hi, (void*)mid, userData) < 0) {
                SWAP(mid, hi, elemSize);
            } else {
                goto jump_over;
            }

            if (cmp((void*)mid, (void*)lo, userData) < 0) {
                SWAP(mid, lo, elemSize);
            }
jump_over:;

            left_ptr = lo + elemSize;
            right_ptr = hi - elemSize;

            // Here's the famous ``collapse the walls'' section of quicksort.
            // Gotta like those tight inner loops!  They are the main reason
            // that this algorithm runs much faster than others.
            do {
                while (cmp((void*)left_ptr, (void*)mid, userData) < 0) {
                    left_ptr += elemSize;
                }

                while (cmp((void*)mid, (void*)right_ptr, userData) < 0) {
                    right_ptr -= elemSize;
                }

                if (left_ptr < right_ptr) {
                    SWAP(left_ptr, right_ptr, elemSize);
                    if (mid == left_ptr) {
                        mid = right_ptr;
                    } else if (mid == right_ptr) {
                        mid = left_ptr;
                    }
                    left_ptr += elemSize;
                    right_ptr -= elemSize;
                } else if (left_ptr == right_ptr) {
                    left_ptr += elemSize;
                    right_ptr -= elemSize;
                    break;
                }
            } while (left_ptr <= right_ptr);

            // Set up pointers for next iteration.  First determine whether
            // left and right partitions are below the threshold size.  If so,
            // ignore one or both.  Otherwise, push the larger partition's
            // bounds on the stack and continue sorting the smaller one.
            if ((uint64_t)(right_ptr - lo) <= max_thresh) {
                if ((uint64_t)(hi - left_ptr) <= max_thresh) {
                    // Ignore both small partitions.
                    POP(lo, hi);
                } else {
                    // Ignore small left partition.
                    lo = left_ptr;
                }
            } else if ((uint64_t)(hi - left_ptr) <= max_thresh) {
                // Ignore small right partition.
                hi = right_ptr;
            } else if ((right_ptr - lo) > (hi - left_ptr)) {
                // Push larger left partition indices.
                PUSH(lo, right_ptr);
                lo = left_ptr;
            } else {
                // Push larger right partition indices.
                PUSH(left_ptr, hi);
                hi = right_ptr;
            }
        }
    }

    // Once the BASE_PTR array is partially sorted by quicksort the rest
    // is completely sorted using insertion sort, since this is efficient
    // for partitions below MAX_THRESH size. BASE_PTR points to the beginning
    // of the array to sort, and END_PTR points at the very last element in
    // the array (*not* one beyond it!).

    {
        const uint8_t* end_ptr = &base_ptr[elemSize * (totalElems - 1)];
        const uint8_t* thresh = end_ptr < (base_ptr + max_thresh) 
            ? end_ptr 
            : (base_ptr + max_thresh)
            ;

        // Find smallest element in first threshold and place it at the
        // array's beginning.  This is the smallest array element,
        // and the operation speeds up insertion sort's inner loop.

        uint8_t* tmp_ptr = base_ptr;
        for (uint8_t* run_ptr = tmp_ptr + elemSize; run_ptr <= thresh; run_ptr += elemSize) {
            if (cmp((void*)run_ptr, (void*)tmp_ptr, userData) < 0) {
                tmp_ptr = run_ptr;
            }
        }

        if (tmp_ptr != base_ptr) {
            SWAP(tmp_ptr, base_ptr, elemSize);
        }

        // Insertion sort, running from left-hand-side up to right-hand-side.

        uint8_t* run_ptr = base_ptr + elemSize;
        while ((run_ptr += elemSize) <= end_ptr) {
            tmp_ptr = run_ptr - elemSize;
            while (cmp((void*)run_ptr, (void*)tmp_ptr, userData) < 0) {
                tmp_ptr -= elemSize;
            }

            tmp_ptr += elemSize;
            if (tmp_ptr != run_ptr) {
                uint8_t* trav;

                trav = run_ptr + elemSize;
                while (--trav >= run_ptr) {
                    uint8_t c = *trav;
                    uint8_t *hi, *lo;

                    for (hi = lo = trav; (lo -= elemSize) >= tmp_ptr; hi = lo) {
                        *hi = *lo;
                    }
                    *hi = c;
                }
            }
        }
    }
}
#undef SWAP
#undef STACK_NOT_EMPTY
#undef POP
#undef PUSH
#undef STACK_SIZE
