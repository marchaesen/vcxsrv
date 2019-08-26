#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    execlp("cmdtest", "cmdtest", ".", NULL);
    perror("Unable to execute 'cmdtest'. Make sure, that it is installed");
    exit(1);
}
