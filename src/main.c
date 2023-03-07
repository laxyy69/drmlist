/*
 * Entry point
 */

#include "drmlist.h"

int main(int argc, const char** argv)
{
    int ret;

    if ((ret = drmlist_init(argc, argv)) == 0)
        ret = drmlist_run();
    
    if (ret == -ENOMEM)
        perror("No memory?");

    drmlist_cleanup();

    return ret;
}