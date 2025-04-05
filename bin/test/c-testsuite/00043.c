// NOTE: Changed struct definition to typedef.
typedef struct s {
    int x;
    struct {
        int y;
        int z;
    } nest;
} s;

int main(void) 
{
    s v;
    v.x = 1;
    v.nest.y = 2;
    v.nest.z = 3;
    if (v.x + v.nest.y + v.nest.z != 6)
        return 1;
    return 0;
}

