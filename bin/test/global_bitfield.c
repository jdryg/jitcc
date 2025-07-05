typedef struct fg_t
{
    unsigned char sortFlags;           /* Mask of KEYINFO_ORDER_* flags */
    unsigned eEName : 2;     /* Meaning of zEName */
    unsigned done : 1;       /* Indicates when processing is finished */
    unsigned reusable : 1;   /* Constant expression is reusable */
    unsigned bSorterRef : 1; /* Defer evaluation until after sorting */
    unsigned bNulls : 1;     /* True if explicit "NULLS FIRST/LAST" */
    unsigned bUsed : 1;      /* This column used in a SF_NestedFrom subquery */
    unsigned bUsingTerm : 1;  /* Term from the USING clause of a NestedFrom */
    unsigned bNoExpand : 1;  /* Term is an auxiliary in NestedFrom and should
                            ** not be expanded by "*" in parent queries */
} fg_t;

static const fg_t g_Zero = { 0 };

int testZero(fg_t* fg)
{
    return 1
        && fg->sortFlags == 0
        && fg->eEName == 0
        && fg->done == 0
        && fg->reusable == 0
        && fg->bSorterRef == 0
        && fg->bNulls == 0
        && fg->bUsed == 0
        && fg->bUsingTerm == 0
        && fg->bNoExpand == 0 ? 0 : 1;
}

int main(void)
{
    return testZero(&g_Zero);
}
