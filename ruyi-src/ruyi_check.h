
#define RUYI_RETURN_IF(cond) \
    do { if (cond) return; } while (false)

#define RUYI_RETURN_VAL_IF(cond, val) \
    do { if (cond) return (val); } while (false)
