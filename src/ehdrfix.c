/* EXE file header fixer, code taken from Morten Welinder */

#include <stdio.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
  int f;
  unsigned short us, test;

  if(argc != 2) {
    printf("Usage: ehdrfix cwsdpmi.exe\n       Updates heap size in exe header\n");
    exit(1);
  }

  f = open(argv[1], O_RDWR | O_BINARY);
  if (f < 0) {
    perror(argv[1]);
    exit(1);
  }

  lseek(f, 0x0aL, SEEK_SET);
  read(f, &us, sizeof(us));
  read(f, &test, sizeof(us));
  if(test == 0xffff) {		/* Not set yet */
    int add,add2;
    FILE *fh;
  
    fh = fopen("control.c","r");
    if(fh) {
      char buffer[256];
      add = 2048;		/* Default _heaplen (can be trimmed) */
      add2 = 0;
      while(!fscanf(fh,"extern unsigned _stklen = %dU", &add2)) {
        if(!fgets(buffer,sizeof(buffer),fh)) break;
      }
      fclose(fh);
      if(add && add2) {
        add /= 16;			/* Kb to paragraphs */
        add2 /= 16;
      } else {
        printf("Can't find stack size in control.c!\n");
        exit(2);
      }
    } else {
      printf("Can't find control.c!\n");
      exit(3);
    }
    
    us += add2;
    lseek(f, 0x0aL, SEEK_SET);
    write(f, &us, sizeof(us));	/* Update min memory */
    us += add;
    write(f, &us, sizeof(us));	/* Update max memory */
  }
  return close(f);
}
