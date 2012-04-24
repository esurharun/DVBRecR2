/* A simple filter (stdin -> stdout) to extract multiple streams from a
   multiplexed TS.  Specify the PID on the command-line 

   Updated 29th January 2003 - Added some error checking and reporting.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
  int pid,n;
  int filters[8192];
  unsigned int i=0;
  unsigned int j=0;
  unsigned char buf[188];
  unsigned char my_cc[8192];
  int errors=0;

  for (i=0;i<8192;i++) { filters[i]=0; my_cc[i]=0xff;}

  for (i=1;i<argc;i++) {
    pid=atoi(argv[i]);
    fprintf(stderr,"Filtering pid %d\n",pid);
    if (pid < 8191) {
      filters[pid]=1;
    }
  }

  n=fread(buf,1,188,stdin);
  i=1;
  while (n==188) {
    if (buf[0]!=0x47) {
      // TO DO: Re-sync.
      fprintf(stderr,"FATAL ERROR IN STREAM AT PACKET %d\n",i);
      exit;
    }
    pid=(((buf[1] & 0x1f) << 8) | buf[2]);
    if (my_cc[pid]==0xff) my_cc[pid]=buf[3]&0x0f;
    if (filters[pid]==1) {
      if (my_cc[pid]!=(buf[3]&0x0f)) {
        fprintf(stderr,"PID %d - packet incontinuity (packet %d)- expected %02x, found %02x\n",my_cc[pid],j,pid,buf[3]&0x0f);
        my_cc[pid]=buf[3]&0x0f;
        errors++;
      }
      n=fwrite(buf,1,188,stdout);
      if (n==188) {
        j++;
      } else {
        fprintf(stderr,"FATAL ERROR - CAN NOT WRITE PACKET %d\n",i);
        exit;
      }
      if (my_cc[pid]==0x0f) {
        my_cc[pid]=0;
      } else {
        my_cc[pid]++;
      }
    }
    n=fread(buf,1,188,stdin);
    i++;
  }
  fprintf(stderr,"Read %d packets, wrote %d.\n",i,j);
  fprintf(stderr,"%d incontinuity errors.\n",errors);
  return(0);
}
