/* 

dvbstream - RTP-ize a DVB transport stream.
(C) Dave Chapman <dave@dchapman.com> 2001, 2002.

The latest version can be found at http://www.linuxstb.org/dvbstream

Copyright notice:

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
    
*/


// Linux includes:
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <resolv.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <values.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <regex.h>
// DVB includes:
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include "rtp.h"
#include "mpegtools/transform.h"
#include "mpegtools/remux.h"

#include "tune.h"

// The default telnet port.
#define DEFAULT_PORT 12345

#define USAGE "\nUSAGE: dvbstream tpid1 tpid2 tpid3 .. tpid8\n\n"
#define PACKET_SIZE 188

// How often (in seconds) to update the "now" variable
#define ALARM_TIME 1

/* Thanks to Giancarlo Baracchino for this fix */
#define MTU 1500
#define IP_HEADER_SIZE 20
#define UDP_HEADER_SIZE 8
#define RTP_HEADER_SIZE 12

#define MAX_RTP_SIZE (MTU-IP_HEADER_SIZE-UDP_HEADER_SIZE-RTP_HEADER_SIZE)

#define writes(f,x) write((f),(x),strlen(x))

/* Signal handling code shamelessly copied from VDR by Klaus Schmidinger 
   - see http://www.cadsoft.de/people/kls/vdr/index.htm */

unsigned int SLOF=(11700*1000UL);
unsigned int LOF1=(9750*1000UL);
unsigned int LOF2=(10600*1000UL);

char* frontenddev[4]={"/dev/dvb/adapter0/frontend0","/dev/dvb/adapter1/frontend0","/dev/dvb/adapter2/frontend0","/dev/dvb/adapter3/frontend0"};
char* dvrdev[4]={"/dev/dvb/adapter0/dvr0","/dev/dvb/adapter1/dvr0","/dev/dvb/adapter2/dvr0","/dev/dvb/adapter3/dvr0"};
char* demuxdev[4]={"/dev/dvb/adapter0/demux0","/dev/dvb/adapter1/demux0","/dev/dvb/adapter2/demux0","/dev/dvb/adapter3/demux0"};

int card=0;
long now;
long real_start_time;
int Interrupted=0;
fe_spectral_inversion_t specInv=INVERSION_AUTO;
int tone=-1;
fe_modulation_t modulation=CONSTELLATION_DEFAULT;
fe_transmit_mode_t TransmissionMode=TRANSMISSION_MODE_DEFAULT;
fe_bandwidth_t bandWidth=BANDWIDTH_DEFAULT;
fe_guard_interval_t guardInterval=GUARD_INTERVAL_DEFAULT;
fe_code_rate_t HP_CodeRate=HP_CODERATE_DEFAULT, LP_CodeRate=LP_CODERATE_DEFAULT;
fe_hierarchy_t hier=HIERARCHY_DEFAULT;
unsigned char diseqc=0;
char pol=0;
int streamtype = RTP;
static int use_stdin=0;

#define PID_MODE 0
#define PROG_MODE 1
static int selection_mode = PID_MODE;


int open_fe(int* fd_frontend) {
    if((*fd_frontend = open(frontenddev[card],O_RDWR | O_NONBLOCK)) < 0){
        perror("FRONTEND DEVICE: ");
        return -1;
    }
    return 1;
}

static void SignalHandler(int signum) {
  struct timeval tv;

  if (signum == SIGALRM) {
    gettimeofday(&tv,(struct timezone*) NULL);
    now=tv.tv_sec-real_start_time;
    alarm(ALARM_TIME);
  } else if (signum != SIGPIPE) {
    Interrupted=signum;
  }
  signal(signum,SignalHandler);
}

long getmsec() {
  struct timeval tv;
  gettimeofday(&tv,(struct timezone*) NULL);
  return(tv.tv_sec%1000000)*1000 + tv.tv_usec/1000;
}

unsigned long int getmsec2() {
  struct timeval tv;
  gettimeofday(&tv,(struct timezone*) NULL);
  return (tv.tv_sec * 1000) + (tv.tv_usec/1000); 

}

// There seems to be a limit of 16 simultaneous filters in the driver
#define MAX_CHANNELS 16



void set_ts_filt(int fd,uint16_t pid, dmx_pes_type_t pestype)
{
  struct dmx_pes_filter_params pesFilterParams;

  //fprintf(stderr,"Setting filter for PID %d\n",pid);
  pesFilterParams.pid     = pid;
  pesFilterParams.input   = DMX_IN_FRONTEND;
  pesFilterParams.output  = DMX_OUT_TS_TAP;
  pesFilterParams.pes_type = pestype;
  pesFilterParams.flags   = DMX_IMMEDIATE_START;

  if (ioctl(fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)  {
    fprintf(stderr,"Failed setting filter for pid %i: ",pid);
    perror("DMX SET PES FILTER");
  }
}

void make_nonblock(int f) {
  int oldflags;

  if ((oldflags=fcntl(f,F_GETFL,0)) < 0) {
    perror("F_GETFL");
  }
  oldflags|=O_NONBLOCK;
  if (fcntl(f,F_SETFL,oldflags) < 0) {
    perror("F_SETFL");
  }
}

typedef enum {STREAM_ON,STREAM_OFF} state_t;


  int socketIn, ns;
  int pids[MAX_CHANNELS];
  int pestypes[MAX_CHANNELS];
  unsigned char hi_mappids[8192];
  unsigned char lo_mappids[8192];
  int fd_frontend;
  int pid,pid2;
  int connectionOpen;
  int fromlen;
  char hostname[64];
  char in_ch;
  struct hostent *hp;
  struct sockaddr_in name, fsin;
  int ReUseAddr=1;
  int oldflags;
  int npids = 0;
  int fd[MAX_CHANNELS];
  int to_stdout = 0; /* to stdout instead of rtp stream */

  /* rtp */
  struct rtpheader hdr;
  struct sockaddr_in sOut;
  int socketOut;

  ipack pa, pv;

#define IPACKS 2048
#define TS_SIZE 188
#define IN_SIZE TS_SIZE



/* The output routine for sending a PS */
void my_write_out(uint8_t *buf, int count,void  *p)
{
  /* to fix: change this buffer size and check for overflow */
  static uint8_t out_buffer[1000000];
  static int out_buffer_n=0;
  int i;

  if (to_stdout) {
    /* This one is easy. */

    write(STDOUT_FILENO, buf, count);
  } else { /* We are streaming it. */
    /* Copy data to write to the end of out_buffer */

    memcpy(&out_buffer[out_buffer_n],buf,count);
    out_buffer_n+=count;

    /* Send as many full packets as possible */

    i=0;
    while ((i + MAX_RTP_SIZE) < out_buffer_n) {
       hdr.timestamp = getmsec()*90;
       sendrtp2(socketOut,&sOut,&hdr,&out_buffer[i],MAX_RTP_SIZE);
       i+=MAX_RTP_SIZE;
    }

    /* Move whatever data is left to the start of the buffer */

    memmove(&out_buffer[0],&out_buffer[i],out_buffer_n-i);
    out_buffer_n-=i;
  }
}

void my_ts_to_ps( uint8_t* buf, uint16_t pida, uint16_t pidv)
{
  uint16_t pid;
  ipack *p;
  uint8_t off = 0;

  pid = get_pid(buf+1);
  if (!(buf[3]&0x10)) // no payload?
    return;
  if (pid == pidv){
    p = &pv;
  } else {
    if (pid == pida){
      p = &pa;
    } else return;
  }

  if ( buf[1]&0x40) {
    if (p->plength == MMAX_PLENGTH-6){
      p->plength = p->found-6;
      p->found = 0;
      send_ipack(p);
      reset_ipack(p);
    }
  }

  if ( buf[3] & 0x20) {  // adaptation field?
    off = buf[4] + 1;
  }
        
  instant_repack(buf+4+off, TS_SIZE-4-off, p);
}


typedef uint8_t PID_BIT_MAP[1024];
static PID_BIT_MAP SI_PIDS;
static PID_BIT_MAP USER_PIDS;


typedef struct {
  char *filename;
  char *onrec_filename;
  int fd;
  int pids[MAX_CHANNELS];
  int num;
  int pid_cnt;
  int progs[MAX_CHANNELS];
  int progs_cnt;
  uint8_t **prognames;
  int prognames_cnt;
  PID_BIT_MAP pidmap;
  long start_time; // in seconds
  long end_time;   // in seconds
  int socket;
  struct rtpheader hdr;
  struct sockaddr_in sOut;
  unsigned char buf[MTU];
  unsigned char net[20];
  int pos;
  int port;
} pids_map_t;

pids_map_t *pids_map;
int map_cnt;

//1024 section payload +1 pointer +256 pointer value
#define SECTION_LEN 1281
typedef struct {
  uint8_t buf[SECTION_LEN];
  unsigned int pos;
} section_t;

typedef struct {
  int program;
  int pmt_pid;
} pat_entry;

static struct {
  int len;	//section length
  int version;
  section_t section;
  pat_entry *entries;
  int entries_cnt;
} PAT;

#define MAX_PIDS 202
typedef struct {
  section_t section;
  int version;
  int pids[MAX_PIDS];
  int pids_cnt;
  uint8_t name[256];
} pmt_t;

struct {
  pmt_t *entries;
  int cnt;
} PMT;

static struct {
  int len;	//section length
  int version;
  section_t section;
} SDT;

static int SI_fd[MAX_CHANNELS];
static int SI_fd_cnt = 0;

#define SDT_PID 0x11

#define getbit(buf, pid) (buf[(pid)/8] & (1 << ((pid) % 8)))
#define setbit(buf, pid) buf[(pid)/8] |= (1 << ((pid) % 8))
#define clearbits(buf) memset(buf, 0, sizeof(PID_BIT_MAP))
#define setallbits(buf) memset(buf, 0xFF, sizeof(PID_BIT_MAP))
#define min(x, y) ((x) <= (y) ? (x) : (y))

void update_bitmaps()
{
  int i, j, k, n;

  for(i = 0; i < map_cnt; i++)
  {
    clearbits(pids_map[i].pidmap);
    setbit(pids_map[i].pidmap, 0);
    for(j = 0; j < MAX_CHANNELS; j++)
    {
      if(pids_map[i].pids[j] == -1) break;
      if(pids_map[i].pids[j] == 8192)
      {
        setallbits(pids_map[i].pidmap);
        break;
      }
      setbit(pids_map[i].pidmap, pids_map[i].pids[j]);
      for(k = 0; k < PMT.cnt; k++)
      {
        for(n = 0; n < PMT.entries[k].pids_cnt; n++)
        {
          if(PMT.entries[k].pids[n] == pids_map[i].pids[j])
          {
            //add the pmt_pid to the map
            //fprintf(stderr, "ADDING TO map %d PMT n. %d with PID: %d, j: %d\n", i, k, PAT.entries[k].pmt_pid, j);
            setbit(pids_map[i].pidmap, PAT.entries[k].pmt_pid);
          }
        }
      }
    }
  }


  for(j = 0; j < map_cnt; j++)
  {
    for(k = 0; k < pids_map[j].progs_cnt; k++)
    {
      for(i = 0; i < PAT.entries_cnt; i++)
      {
        if(pids_map[j].progs[k] == PAT.entries[i].program)
        {
          setbit(pids_map[j].pidmap, PAT.entries[i].pmt_pid);
          setbit(pids_map[j].pidmap, SDT_PID);
          for(n = 0; n < PMT.entries[i].pids_cnt; n++)
          {
            int pid = PMT.entries[i].pids[n];

            setbit(pids_map[j].pidmap, pid);
            //fprintf(stderr, "\nADDED to map %d PROG pid %d, prog: %d", j, pid, PAT.entries[i].program);
          }
        }
      }
    }
  }


  for(i = 0; i < map_cnt; i++)
  {
    for(j = 0; j < pids_map[i].prognames_cnt; j++)
    {
      for(k = 0; k < PMT.cnt; k++)
      {
        if(!strcmp(pids_map[i].prognames[j], PMT.entries[k].name))
        {
          setbit(pids_map[i].pidmap, PAT.entries[k].pmt_pid);
          setbit(pids_map[i].pidmap, SDT_PID);
          for(n = 0; n < PMT.entries[k].pids_cnt; n++)
          {
            int pid = PMT.entries[k].pids[n];

            setbit(pids_map[i].pidmap, pid);
            //fprintf(stderr, "\nADDED to map %d PROG pid %d, prog: %d", j, pid, PAT.entries[k].program);
          }
        }
      }
    }
  }
}


static int collect_section(section_t *section, int pusi, uint8_t *buf, unsigned int len)
{
  int skip, slen;
  uint8_t *ptr;

  if(pusi)
    section->pos = 0;

  if(section->pos + len > SECTION_LEN)
    return 0;

  memcpy(&(section->buf[section->pos]), buf, len);
  section->pos += len;

  skip = section->buf[0];
  if(skip + 4 > section->pos)
    return 0;

  ptr = &(section->buf[skip + 1]);
  slen = ((ptr[1] & 0x0f) << 8) | ptr[2];
  if(section->pos < (skip+1+3+slen))
    return 0;

  return skip+1;
}


static int parse_pat(int pusi, uint8_t *b, int l)
{
  unsigned int i, j, vers, seclen, num, skip;
  uint8_t *buf;

  skip = collect_section(&PAT.section, pusi, b, l);
  if(!skip)
    return 0;

  //now we know the section is complete
  PAT.section.pos = 0;
  buf = &(PAT.section.buf[skip]);

  if(buf[0] != 0) //pat id
    return 0;
  if(!(buf[5] & 1)) //not yet valid
    return 0;

  vers = (buf[5] >> 1) & 0x1F;
  if(PAT.version == vers) //PAT didn't change
    return 1;

  clearbits(SI_PIDS);
  setbit(SI_PIDS, 0);
  setbit(SI_PIDS, SDT_PID);
  seclen = ((buf[1] & 0x0F) << 8) | buf[2];
  num = (seclen - 9) / 4;
  if(PAT.entries_cnt != num)
  {
    PAT.entries = realloc(PAT.entries, sizeof(pat_entry)*num);
    PAT.entries_cnt = num;
    PMT.entries = realloc(PMT.entries, sizeof(pmt_t)*num);
    if(!PMT.entries) return 0;
    PMT.cnt = num;
  }

  i = 8;
  j = 0;
  for(j=0; j<num; j++)
  {
    PAT.entries[j].program = (buf[i] << 8) | buf[i+1];
    PAT.entries[j].pmt_pid = ((buf[i+2] & 0x1F) << 8) | buf[i+3];
    setbit(SI_PIDS, PAT.entries[j].pmt_pid);
    i += 4;
    //fprintf(stderr, "PROGRAM: %d, pmt_pid: %d\n", PAT.entries[j].program, PAT.entries[j].pmt_pid);
    PMT.entries[j].section.pos = SECTION_LEN+1;
    PMT.entries[j].version = -1;
    PMT.entries[j].name[0] = 0;
  }
  SDT.version=-1;
  SDT.section.pos = SECTION_LEN+1;

  PAT.version = vers;

  return 2;
}

static void add_pmt_pids()
{
  int i;
  PID_BIT_MAP simap;

  for(i = 0; i < SI_fd_cnt; i++)
    close(SI_fd[i]);

  clearbits(simap);
  setbit(simap, 0);
  for(i=0; i<min(PAT.entries_cnt, MAX_CHANNELS); i++)
  {
    if(getbit(USER_PIDS, PAT.entries[i].pmt_pid)) continue;
    if(getbit(simap, PAT.entries[i].pmt_pid)) continue;
    if((SI_fd[i] = open(demuxdev[card], O_RDWR|O_NONBLOCK)) < 0)
    {
      fprintf(stderr,"COULDN'T OPEN DEMUX %i: for pid: %d", i, PAT.entries[i].pmt_pid);
      return;
    }
    //fprintf(stderr, "\nADDED PMT PID: %d\n", PAT.entries[i].pmt_pid);
    set_ts_filt(SI_fd[i], PAT.entries[i].pmt_pid, DMX_PES_OTHER);
    setbit(simap, PAT.entries[i].pmt_pid);
    SI_fd_cnt++;
  }
}

static int parse_pmt(int pusi, pmt_t *pmt, uint8_t *b, int l)
{
  unsigned int i, version, seclen, skip, prog, pcr_pid, pid;
  uint8_t *buf;

  skip = collect_section(&(pmt->section), pusi, b, l);

  if(!skip)
    return 0;

  //now we know the section is complete
  pmt->section.pos = 0;
  pmt->pids_cnt = 0;
  buf = &(pmt->section.buf[skip]);

  if(buf[0] != 2) //pmt id
    return 0;
  if(!(buf[5] & 1)) //not yet valid
    return 0;

  prog = (buf[3] << 8) | buf[4];
  version = (buf[5] >> 1) & 0x1F;

  if(pmt->version == version) //PMT didn't change
    return 1;

  seclen = ((buf[1] & 0x0F) << 8) | buf[2];
  pcr_pid = ((buf[8] & 0x1F) << 8) | buf[9];
  pmt->pids[pmt->pids_cnt++] = pcr_pid;
  //fprintf(stderr, "\nPROGRAM: %d, pcr_pid: %d, version: %d vs %d\n", prog, pcr_pid, pmt->version, version);
  skip = ((buf[10] & 0x0F) << 8) | buf[11];
  if(skip+12 > seclen)
    return 0;

  i = skip+12;
  while(i+5<seclen)
  {
    pid = ((buf[i+1] & 0x1F) << 8) | buf[i+2];
    pmt->pids[pmt->pids_cnt++] = pid;
    skip = ((buf[i+3] & 0x0F) << 8) | buf[i+4];
    i += skip+5;
    //fprintf(stderr, "prog %d, PID: %d, count: %d, type: 0x%x\n", prog, pid, pmt->pids_cnt, buf[i]);
  }
  pmt->version = version;
  return 2;
}

static int parse_sdt(int pusi, uint8_t *b, int l)
{
  unsigned int i, version, seclen, skip, prog, k, descr_len, found, len;
  uint8_t *buf;

  skip = collect_section(&(SDT.section), pusi, b, l);

  if(!skip)
    return 0;

  buf = &(SDT.section.buf[skip]);

  if(buf[0] != 0x42) //pmt id
    return 0;
  if(!(buf[5] & 1)) //not yet valid
    return 0;

  version = (buf[5] >> 1) & 0x1F;

  if(SDT.version == version) //SDT didn't change
    return 1;

  seclen = ((buf[1] & 0x0F) << 8) | buf[2];
  if(seclen < 12)
    return 0;

  i = 11;
  while(i  < seclen - 4)
  {
    descr_len = ((buf[i+3] & 0x0F) << 8) | buf[i+4];
    if(i+5+descr_len >= seclen)
      break;
    prog = (buf[i] << 8) | buf[i+1];
    found = -1;
    k = 0;
    for(k = 0; k < PAT.entries_cnt, k < PMT.cnt; k++)
      if(PAT.entries[k].program == prog)
      {
        found = k;
        

    if(k != -1)
    {
      int j, len, dlen;

      j = i + 5;
      len = dlen = 0;
      while(len < descr_len)
      {
        int provider_len, name_len, n;
        pmt_t *pmt;

        n = j;
        dlen = buf[n+1];
        if(len + dlen > descr_len) break;
        if(buf[n] == 0x48)
        {
          provider_len = buf[n+3];
          if(provider_len + 2 > dlen)
            break;
          n += 4 + provider_len;
          name_len = buf[n];
          if(provider_len + 3 + name_len > dlen)
            break;
          pmt = &PMT.entries[k];
          memcpy(pmt->name, &buf[n+1], name_len);
          pmt->name[name_len] = 0;
          fprintf(stderr, "Program n. %d, name: '%s'\n", prog, pmt->name);
        }
        len += dlen+2;
        j += dlen+2;
      }
    }
    }
    i += 5 + descr_len;
  }
  SDT.version = version;
  return 2;
}


static int parse_ts_packet(uint8_t *buf)
{
  int pid, l, af, pusi;

  if(buf[0] != 0x47)
    return 0;
  pusi = buf[1] & 0x40;
  pid = ((buf[1] & 0x1F) << 8) | buf[2];
  af = (buf[3] >> 4) & 0x03;
  l = 4;
  if(af == 2) //only adaption
    return 0;
  else if(af == 3)
    l += buf[4] + 1;
  if(l >= TS_SIZE - 4)
    return 0;

  if(pid == 0)
  {
    if(parse_pat(pusi, &buf[l], TS_SIZE - l) == 2)
      add_pmt_pids();
  }
  else if(pid == SDT_PID)
  {
    if(parse_sdt(pusi, &buf[l], TS_SIZE - l) == 2)
    {
      int i;
      for(i = 0; i < PMT.cnt; i++)
      {
        PMT.entries[i].section.pos = SECTION_LEN+1;
        PMT.entries[i].version = -1;
      }
      update_bitmaps();
    }
  }
  else
  {
    int i;

    for(i=0; i<PAT.entries_cnt; i++)
    {
      if(pid==PAT.entries[i].pmt_pid)
      {
        if(parse_pmt(pusi, &PMT.entries[i], &buf[l], TS_SIZE - l) == 2)
          update_bitmaps();
      }
    }
  }
}

static int is_string(char *s)
{
  int i, n, len;

  n = 0;
  len = strlen(s);
  for(i = 0; i < len; i++)
    if(isdigit(s[i]))
      n++;

  return (n != len);
}





void renew_output_file() {


	int i;
	for (i=0;i<map_cnt;i++) {
	       if(pids_map[i].filename) {
		       	FILE *f;
	       		char *newFilename;
	        	asprintf(&newFilename, "%s.%li.onrec",pids_map[i].filename,getmsec2());
			

			if (pids_map[i].onrec_filename != NULL && pids_map[i].fd != -1) {
		
				close(pids_map[i].fd); 
				//if (close(pids_map[i].fd) == 0) {
					char *to_rename;
					to_rename = (char *) malloc(strlen(pids_map[i].onrec_filename) );
					memset(to_rename,' ',strlen(to_rename));
					strncpy(to_rename,pids_map[i].onrec_filename,strlen(pids_map[i].onrec_filename)-6);

					char *to_rename_end;

					asprintf(&to_rename_end, "%s-%li", to_rename, getmsec2());

					if (rename(pids_map[i].onrec_filename,to_rename_end) != -1) {
						fprintf(stderr, "Finished: %s \n", to_rename_end); 
					} else {
						fprintf(stderr, "Failed to rename: %s to %s \n", pids_map[i].onrec_filename,to_rename_end);
					}
					
				//} else {
				//	fprintf(stderr, "Cannot close file: %s\n", pids_map[i].onrec_filename);
				//}
				

			}
			 
			f = fopen(newFilename, "w+b");
			if (f != NULL) {
		       		pids_map[i].onrec_filename = newFilename;
				pids_map[i].fd = fileno(f);
		       		make_nonblock(pids_map[i].fd);
		       		fprintf(stderr, "Open file %s\n", newFilename);
			} else {
		       		pids_map[i].fd = -1;
				pids_map[i].onrec_filename = NULL;
		       		fprintf(stderr, "Couldn't open file %s, errno:%d\n", newFilename, errno);
			}
		}
	}


}

int main(int argc, char **argv)
{
  //  state_t state=STREAM_OFF;
  int fd_dvr;
  int i,j;
  uint8_t buf[MTU];
  struct pollfd pfds[2];  // DVR device and Telnet connection
  unsigned int secs = -1;
  unsigned int interval_secs = -1;
  unsigned long freq=0;
  unsigned long srate=0;
  int count;
  char* ch;
  dmx_pes_type_t pestype;
  int bytes_read;
  int do_analyse=0;
  unsigned char* free_bytes;
  int output_type=RTP_TS;
  int64_t counts[8192];
  double f;
  long start_time=-1;
  long end_time=-1;
  struct timeval tv;
  int found;
  int stream_whole_TS=0;

  /* Output: {uni,multi,broad}cast socket */
  char ipOut[20];
  int portOut;
  int ttl = 2;
  
  pids_map = NULL;
  map_cnt = 0;

  gettimeofday(&tv,(struct timezone*) NULL);
  real_start_time=tv.tv_sec;
  now=0;



  fprintf(stderr,"dvbstream v0.7 - (C) Dave Chapman 2001-2004\n");
  fprintf(stderr,"Modified by Harun Esur - 2012\n");

  /* Initialise PID map */
  for (i=0;i<8192;i++) {
    hi_mappids[i]=(i >> 8);
    lo_mappids[i]=(i&0xff);
    counts[i]=0;
  }
  memset(&PAT, 0, sizeof(PAT));
  PAT.version = -1;
  PAT.section.pos = SECTION_LEN+1;
  memset(&PMT, 0, sizeof(PMT));
  clearbits(SI_PIDS);
  setbit(SI_PIDS, 0);
  memset(&SDT, 0, sizeof(SDT));
  SDT.section.pos = SECTION_LEN+1;
  SDT.version = -1;
  setbit(SI_PIDS, SDT_PID);
  clearbits(USER_PIDS);
  setbit(USER_PIDS, 0);

  /* Set default IP and port */
  strcpy(ipOut,"224.0.1.2");
  portOut = 5004;

  if (argc==1) {
    fprintf(stderr,"Usage: dvbtune [OPTIONS] pid1 pid2 ... pid8\n\n");
    fprintf(stderr,"-o          Stream to stdout instead of network\n");
    fprintf(stderr,"-o:file.ts  Stream to named file instead of network\n");
    fprintf(stderr,"-n secs,secs2     Stop after secs seconds and restart at secs2 interval\n");
    fprintf(stderr,"-ps         Convert stream to Program Stream format (needs exactly 2 pids)\n");
    fprintf(stderr,"-v vpid     Decode video PID (full cards only)\n");
    fprintf(stderr,"-a apid     Decode audio PID (full cards only)\n");
    fprintf(stderr,"-t ttpid    Decode teletext PID (full cards only)\n");
    fprintf(stderr,"\nStandard tuning options:\n\n");
    fprintf(stderr,"-f freq     absolute Frequency (DVB-S in Hz or DVB-T in Hz)\n");
    fprintf(stderr,"            or L-band Frequency (DVB-S in Hz or DVB-T in Hz)\n");
    fprintf(stderr,"-SL slof    S-LOF(DVB-S only)\n");
    fprintf(stderr,"-L1 LOF1    LOF1(DVB-S only)\n");
    fprintf(stderr,"-L2 LOF2    LOF2(DVB-S only)\n");
    fprintf(stderr,"-p [H,V]    Polarity (DVB-S only)\n");
    fprintf(stderr,"-s N        Symbol rate (DVB-S or DVB-C)\n");

    fprintf(stderr,"\nAdvanced tuning options:\n\n");
    fprintf(stderr,"-c [0-3]    Use DVB card #[0-3]\n");
    fprintf(stderr,"-D [0-4AB]  DiSEqC command (0=none)\n\n");
    fprintf(stderr,"-I [0|1|2]  0=Spectrum Inversion off, 1=Spectrum Inversion on, 2=auto\n");
    fprintf(stderr,"-qam X      DVB-T/C and ATSC modulation - 16%s, 32%s, 64%s, 128%s or 256%s\n",(CONSTELLATION_DEFAULT==QAM_16 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_32 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_64 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_128 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_256 ? " (default)" : ""));
#ifdef DVB_ATSC
    fprintf(stderr,"-vsb X      ATSC modulation - 8, 16\n");
#endif
    fprintf(stderr,"-gi N       DVB-T guard interval 1_N (N=32%s, 16%s, 8%s or 4%s)\n",(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_32 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_16 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_8 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_4 ? " (default)" : ""));
    fprintf(stderr,"-cr N       DVB-T/C code rate. N=AUTO%s, 1_2%s, 2_3%s, 3_4%s, 5_6%s, 7_8%s\n",(HP_CODERATE_DEFAULT==FEC_AUTO ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_1_2 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_2_3 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_3_4 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_5_6 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_7_8 ? " (default)" : ""));
    fprintf(stderr,"-crlp N     DVB-T code rate LP. N=AUTO%s, 1_2%s, 2_3%s, 3_4%s, 5_6%s, 7_8%s\n",(LP_CODERATE_DEFAULT==FEC_AUTO ? " (default)" : ""),(LP_CODERATE_DEFAULT==FEC_1_2 ? " (default)" : ""),(LP_CODERATE_DEFAULT==FEC_2_3 ? " (default)" : ""),(LP_CODERATE_DEFAULT==FEC_3_4 ? " (default)" : ""),(LP_CODERATE_DEFAULT==FEC_5_6 ? " (default)" : ""),(LP_CODERATE_DEFAULT==FEC_7_8 ? " (default)" : ""));
    fprintf(stderr,"-bw N       DVB-T bandwidth (Mhz) - N=6%s, 7%s or 8%s\n",(BANDWIDTH_DEFAULT==BANDWIDTH_6_MHZ ? " (default)" : ""),(BANDWIDTH_DEFAULT==BANDWIDTH_7_MHZ ? " (default)" : ""),(BANDWIDTH_DEFAULT==BANDWIDTH_8_MHZ ? " (default)" : ""));
    fprintf(stderr,"-tm N       DVB-T transmission mode - N=2%s or 8%s\n",(TRANSMISSION_MODE_DEFAULT==TRANSMISSION_MODE_2K ? " (default)" : ""),(TRANSMISSION_MODE_DEFAULT==TRANSMISSION_MODE_8K ? " (default)" : ""));
    fprintf(stderr,"-hy N       DVB-T hierarchy - N=1%s, 2%s, 4%s, NONE%s or AUTO%s\n",(HIERARCHY_DEFAULT==HIERARCHY_1 ? " (default)" : ""),(HIERARCHY_DEFAULT==HIERARCHY_2 ? " (default)" : ""),(HIERARCHY_DEFAULT==HIERARCHY_4 ? " (default)" : ""),(HIERARCHY_DEFAULT==HIERARCHY_NONE ? " (default)" : ""),(HIERARCHY_DEFAULT==HIERARCHY_AUTO ? " (default)" : ""));
    fprintf(stderr,"-ttl N      Sets TTL to N (default: 2) when streaming in RTP\n");
    fprintf(stderr,"-prog       Selects PROGRAM mode (opens a demux on the whole TS)\n");
    fprintf(stderr,"-pid        Selects PID mode (default)\n");
    fprintf(stderr,"-stdin      Use STDIN as source rather than a DVB card\n");


    fprintf(stderr,"\n-analyse    Perform a simple analysis of the bitrates of the PIDs in the transport stream\n");

    fprintf(stderr,"\n");
    fprintf(stderr,"NOTE: Use pid1=8192 to broadcast whole TS stream from a budget card\n");
    return(-1);
  } else {
    pids[0]=0;
    npids=1;
    pestype=DMX_PES_OTHER;  // Default PES type
    for (i=1;i<argc;i++) {
      if (strcmp(argv[i],"-ps")==0) {
        output_type=RTP_PS;
      } else if (strcmp(argv[i],"-analyse")==0) {
        do_analyse=1;
        output_type=RTP_NONE;
        if (secs==-1) { secs=10; }
      } else if(strcmp(argv[i],"-stdin")==0) {
        use_stdin = 1;
      } else if (strcmp(argv[i],"-i")==0) {
        if(pids_map != NULL) {
	  fprintf(stderr, "ERROR! -i and -r can't be used with -o and -net.  Use -net instead\n");
	  exit(1);
	}
        i++;
        strcpy(ipOut,argv[i]);
      } else if(strcmp(argv[i],"-auto")==0) {
        modulation = QAM_AUTO;
        TransmissionMode = TRANSMISSION_MODE_AUTO;
        guardInterval = GUARD_INTERVAL_AUTO;
        HP_CodeRate = FEC_AUTO;
        LP_CodeRate = FEC_AUTO;
        hier = HIERARCHY_AUTO;
	specInv = INVERSION_AUTO;
      }
      else if (strcmp(argv[i],"-r")==0) {
        if(pids_map != NULL) {
	  fprintf(stderr, "ERROR! -i and -r can't be used with -o and -net.  Use -net instead\n");
	  exit(1);
	}
        i++;
        portOut=atoi(argv[i]);
      } else if (strcmp(argv[i],"-f")==0) {
        i++;
        freq=atoi(argv[i]);
      } else if (strcmp(argv[i],"-p")==0) {
        i++;
        if (argv[i][1]==0) {
          if (tolower(argv[i][0])=='v') {
            pol='V';
          } else if (tolower(argv[i][0])=='h') {
            pol='H';
          }
        }
      } else if (strcmp(argv[i],"-SL")==0) {
        i++;
        SLOF=atoi(argv[i]);
        SLOF*=1000UL;
      } else if (strcmp(argv[i],"-L1")==0) {
        i++;
        LOF1=atoi(argv[i]);
        LOF1*=1000UL;
      } else if (strcmp(argv[i],"-L2")==0) {
        i++;
        LOF2=atoi(argv[i]);
        LOF2*=1000UL;
      }
      else if (strcmp(argv[i],"-s")==0) {
        i++;
        srate=atoi(argv[i])*1000UL;
      } 
      else if (strcmp(argv[i],"-D")==0) 
      {
        i++;
	diseqc = argv[i][0];
	if(toupper(diseqc) == 'A')
	    diseqc = 'A';
	else if(toupper(diseqc) == 'B')
	    diseqc = 'B';
	else if(diseqc >= '0' && diseqc <= '4') {
	    diseqc=diseqc - '0';
	}
	else {
	        fprintf(stderr,"DiSEqC must be between 0 and 4 or A | B\n");
	        exit(-1);
	}
      } else if (strcmp(argv[i],"-I")==0) {
        i++;
        if (atoi(argv[i])==0)
           specInv = INVERSION_OFF;
        else if (atoi(argv[i])==1)
           specInv = INVERSION_ON;
        else
           specInv = INVERSION_AUTO;
      }
      else if(strcmp(argv[i],"-prog")==0) {
        selection_mode = PROG_MODE;
      }
      else if(strcmp(argv[i],"-pid")==0) {
        selection_mode = PID_MODE;
      }
      else if (strcmp(argv[i],"-o")==0) {
        to_stdout = 1;
      } else if (strcmp(argv[i],"-n")==0) {
        i++;
       
	regex_t myregex ; 

	int rc;
	rc = regcomp(&myregex, "[0-9]+,[0-9]+", REG_EXTENDED | REG_NOSUB);

	if (!regexec(&myregex, argv[i], 0, 0, 0) ) {
	
		secs = atoi(strtok(argv[i], ","));
		interval_secs = atoi(strtok(NULL, ","));

		fprintf(stderr, "Stream will be recorded for %i secs and will turn to %i interval\n",secs,interval_secs);
	
	} else {
		fprintf(stderr, "ERROR: -n parameter is not matching as it requires\n");
		exit(-1);
	
	}






      } else if (strcmp(argv[i],"-c")==0) {
        i++;
        card=atoi(argv[i]);
        if ((card < 0) || (card > 3)) {
          fprintf(stderr,"ERROR: card parameter must be between 0 and 4\n");
        }
      } else if (strcmp(argv[i],"-v")==0) {
        pestype=DMX_PES_VIDEO;
      } else if (strcmp(argv[i],"-a")==0) {
        pestype=DMX_PES_AUDIO;
      } else if (strcmp(argv[i],"-t")==0) {
        pestype=DMX_PES_TELETEXT;
      } else if (strcmp(argv[i],"-qam")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 16:  modulation=QAM_16; break;
          case 32:  modulation=QAM_32; break;
          case 64:  modulation=QAM_64; break;
          case 128: modulation=QAM_128; break;
          case 256: modulation=QAM_256; break;
          default:
            fprintf(stderr,"Invalid QAM rate: %s\n",argv[i]);
            exit(0);
        }
      } 
#ifdef DVB_ATSC
     else if(strcmp(argv[i],"-vsb")==0) {
       i++;
        switch(atoi(argv[i])) {
	  case 8:  modulation=VSB_8; break;
          case 16:  modulation=VSB_16; break;
          default:
            fprintf(stderr,"Invalid ATSC VSB modulation: %s\n",argv[i]);
            exit(0);
        }
      }
#endif
      else if (strcmp(argv[i],"-gi")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 32:  guardInterval=GUARD_INTERVAL_1_32; break;
          case 16:  guardInterval=GUARD_INTERVAL_1_16; break;
          case 8:   guardInterval=GUARD_INTERVAL_1_8; break;
          case 4:   guardInterval=GUARD_INTERVAL_1_4; break;
          default:
            fprintf(stderr,"Invalid Guard Interval: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-tm")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 8:   TransmissionMode=TRANSMISSION_MODE_8K; break;
          case 2:   TransmissionMode=TRANSMISSION_MODE_2K; break;
          default:
            fprintf(stderr,"Invalid Transmission Mode: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-bw")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 8:   bandWidth=BANDWIDTH_8_MHZ; break;
          case 7:   bandWidth=BANDWIDTH_7_MHZ; break;
          case 6:   bandWidth=BANDWIDTH_6_MHZ; break;
          default:
            fprintf(stderr,"Invalid DVB-T bandwidth: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-cr")==0) {
        i++;
        if (!strcmp(argv[i],"AUTO")) {
          HP_CodeRate=FEC_AUTO;
        } else if (!strcmp(argv[i],"1_2")) {
          HP_CodeRate=FEC_1_2;
        } else if (!strcmp(argv[i],"2_3")) {
          HP_CodeRate=FEC_2_3;
        } else if (!strcmp(argv[i],"3_4")) {
          HP_CodeRate=FEC_3_4;
        } else if (!strcmp(argv[i],"5_6")) {
          HP_CodeRate=FEC_5_6;
        } else if (!strcmp(argv[i],"7_8")) {
          HP_CodeRate=FEC_7_8;
        } else {
          fprintf(stderr,"Invalid Code Rate: %s\n",argv[i]);
          exit(0);
        }
      } else if (strcmp(argv[i],"-crlp")==0) {
        i++;
        if (!strcmp(argv[i],"AUTO")) {
          LP_CodeRate=FEC_AUTO;
        } else if (!strcmp(argv[i],"1_2")) {
          LP_CodeRate=FEC_1_2;
        } else if (!strcmp(argv[i],"2_3")) {
          LP_CodeRate=FEC_2_3;
        } else if (!strcmp(argv[i],"3_4")) {
          LP_CodeRate=FEC_3_4;
        } else if (!strcmp(argv[i],"5_6")) {
          LP_CodeRate=FEC_5_6;
        } else if (!strcmp(argv[i],"7_8")) {
          LP_CodeRate=FEC_7_8;
        } else {
          fprintf(stderr,"Invalid Code Rate LP: %s\n",argv[i]);
          exit(0);
        }
      } else if (strcmp(argv[i],"-hier")==0) {
        i++;
        if (!strcmp(argv[i],"AUTO")) {
          hier=HIERARCHY_AUTO;
        } else if (!strcmp(argv[i],"1")) {
          hier=HIERARCHY_2;
        } else if (!strcmp(argv[i],"4")) {
          hier=HIERARCHY_4;
        } else if (!strcmp(argv[i],"NONE")) {
          hier=HIERARCHY_NONE;
        } else {
          fprintf(stderr,"Invalid HIERARCHY: %s\n",argv[i]);
          exit(0);
        }
      } else if (strcmp(argv[i],"-ttl")==0) {
        i++;
	ttl = atoi(argv[i]);
      } else if (strstr(argv[i], "-o:")==argv[i]) {
        if (strlen(argv[i]) > 3) {
	  char * fname;
	  fname = (char *) malloc(strlen(argv[i]) - 2);
	  if(fname == NULL) {
	  	fprintf(stderr, "Couldn't alloc enough memory for this -o: entry, discarding\n");
	  } else {
	    strcpy(fname, &argv[i][3]);
            pids_map = (pids_map_t*) realloc(pids_map, sizeof(pids_map_t) * (map_cnt+1));
	    if(pids_map != NULL) {
	      map_cnt++;
              pids_map[map_cnt-1].pid_cnt = 0;
              pids_map[map_cnt-1].start_time=start_time;
              pids_map[map_cnt-1].end_time=end_time;
              for(j=0; j < MAX_CHANNELS; j++) pids_map[map_cnt-1].pids[j] = -1;
              pids_map[map_cnt-1].filename = fname;

              output_type = MAP_TS;
	    } else
	        fprintf(stderr, "Couldn't alloc enough memory for file %s: entry, discarding\n", fname);
         }
        }
      } else {
        if ((ch=(char*)strstr(argv[i],":"))!=NULL) {
          pid2=atoi(&ch[1]);
          ch[0]=0;
        } else {
          pid2=-1;
        }
        pid=atoi(argv[i]);

        // If we are currently processing a "-o:" option:
        if (map_cnt) {
          if(selection_mode == PID_MODE) {
          // block for the map
          found = 0;
          for (j=0;j<MAX_CHANNELS;j++) {
            if(pids_map[map_cnt-1].pids[j] == pid) found = 1;
          }
          if (found == 0) {
            if(pids_map[map_cnt-1].pid_cnt==0) {
              pids_map[map_cnt-1].pids[0]=0;
              pids_map[map_cnt-1].pid_cnt++;
            }
            if(pid==8192) {
              fprintf(stderr, "Adding whole transport stream to map n. %d\n", map_cnt-1);
              setallbits(USER_PIDS);
            }
            pids_map[map_cnt-1].pids[pids_map[map_cnt-1].pid_cnt] = pid;
            pids_map[map_cnt-1].pid_cnt++;
          }
          }
          else {
          // block for the map
          int is_progname = is_string(argv[i]);
          pids_map_t *map = &(pids_map[map_cnt-1]);
          stream_whole_TS=1;
          setallbits(USER_PIDS);
          found = 0;
          if(is_progname) {
            for(j=0;j<map->prognames_cnt;j++) {
              if(!strcmp(map->prognames[j], argv[i]))
                found = 1;
            }
            if(found == 0) {
             map->prognames = realloc(map->prognames, (map->prognames_cnt+1)*sizeof(map->prognames));
             map->prognames_cnt++;
             map->prognames[map->prognames_cnt-1] = malloc(strlen(argv[i])+1);
             strcpy(map->prognames[map->prognames_cnt-1], argv[i]);
            }
          }
          else {
          for (j=0;j<MAX_CHANNELS;j++) {
            if(pids_map[map_cnt-1].progs[j] == pid) found = 1;
          }
          if(found == 0)
            pids_map[map_cnt-1].progs[pids_map[map_cnt-1].progs_cnt++] = pid;
        }
        }
        }

        if(selection_mode == PID_MODE) {
        // block for the list of pids to demux
        found = 0;
        for (j=0;j<npids;j++) {
          if(pids[j] == pid) found = 1;
        }
        if (found==0) {
          if (npids == MAX_CHANNELS) {
            fprintf(stderr,"\nSorry, you can only set up to %d filters.\n\n",MAX_CHANNELS);
            return(-1);
          } else {
            pestypes[npids]=pestype;
            pestype=DMX_PES_OTHER;
            pids[npids++]=pid;
            if (pid2!=-1) {
              hi_mappids[pid]=pid2>>8;
              lo_mappids[pid]=pid2&0xff;
              fprintf(stderr,"Mapping %d to %d\n",pid,pid2);
            }
          }
        }
      }
      }
    }
  }

  if ((output_type==RTP_PS) && (npids!=3)) {
    fprintf(stderr,"ERROR: PS requires exactly two PIDS - video and audio.\n");
    exit(1);
  }

 update_bitmaps();

  if (signal(SIGHUP, SignalHandler) == SIG_IGN) signal(SIGHUP, SIG_IGN);
  if (signal(SIGINT, SignalHandler) == SIG_IGN) signal(SIGINT, SIG_IGN);
  if (signal(SIGTERM, SignalHandler) == SIG_IGN) signal(SIGTERM, SIG_IGN);
  if (signal(SIGALRM, SignalHandler) == SIG_IGN) signal(SIGALRM, SIG_IGN);
  alarm(ALARM_TIME);

  if (freq!=0 && !use_stdin) {
    if (open_fe(&fd_frontend)) {
      fprintf(stderr,"Tuning to %ld Hz\n",freq);
      i=tune_it(fd_frontend,freq,srate,pol,tone,specInv,diseqc,modulation,HP_CodeRate,TransmissionMode,guardInterval,bandWidth, LP_CodeRate, hier);
    }
  }

  if (i<0) { exit(i); }

  if(map_cnt > 0)
    fprintf(stderr, "\n");
  for (i=0;i<map_cnt;i++) {
    if ((secs==-1) || (secs < pids_map[i].end_time)) { secs=pids_map[i].end_time; }
    if(pids_map[i].filename != NULL)
    	fprintf(stderr,"MAP %d, file %s: From %ld secs, To %ld secs, %d PIDs - ",i,pids_map[i].filename,pids_map[i].start_time,pids_map[i].end_time,pids_map[i].pid_cnt);
    else
        fprintf(stderr,"MAP %d, addr %s:%d From %ld secs, To %ld secs, %d PIDs - ",i,pids_map[i].net,pids_map[i].port,pids_map[i].start_time,pids_map[i].end_time,pids_map[i].pid_cnt);
    for (j=0;j<MAX_CHANNELS;j++) { if (pids_map[i].pids[j]!=-1) fprintf(stderr," %d",pids_map[i].pids[j]); }
    fprintf(stderr,"\n");
  }
  
  fprintf(stderr,"dvbstream will stop after %d seconds (%d minutes)\n",secs,secs/60);

  if(stream_whole_TS) {
    npids=1;
    pids[0] = 8192;
  }
  else
  for(i=0; i<npids; i++) {
    if(pids[i] == 8192) {
      npids = 1;
      pids[0] = 8192;
    }
  }
  if(use_stdin) {
    fd_dvr = fileno(stdin);
  } else {
  for (i=0;i<npids;i++) {  
    if((fd[i] = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
    }
  }

  if((fd_dvr = open(dvrdev[card],O_RDONLY|O_NONBLOCK)) < 0){
    perror("DVR DEVICE: ");
    return -1;
  }

  /* Now we set the filters */
  for (i=0;i<npids;i++) {
    set_ts_filt(fd[i],pids[i],pestypes[i]);
    setbit(USER_PIDS, pids[i]);
  }
  }

   renew_output_file();

  if (do_analyse) {
    fprintf(stderr,"Analysing PIDS\n");
  } else {
    if (to_stdout) {
      fprintf(stderr,"Output to stdout\n");
    }
    else if(! map_cnt) {
      ttl = 2;
      fprintf(stderr,"Using %s:%d:%d\n",ipOut,portOut,ttl);

      /* Init RTP */
      socketOut = makesocket(ipOut,portOut,ttl,&sOut);
      #warning WHAT SHOULD THE PAYLOAD TYPE BE FOR "MPEG-2 PS" ?
      initrtp(&hdr,(output_type==RTP_TS ? 33 : 34), streamtype);
      fprintf(stderr,"version=%X\n",hdr.b.v);
    }
    fprintf(stderr,"Streaming %d stream%s\n",npids,(npids==1 ? "" : "s"));
  }

  if (output_type==RTP_PS) {
    init_ipack(&pa, IPACKS,my_write_out, 1);
    init_ipack(&pv, IPACKS,my_write_out, 1);
  }

  /* Read packets */
  free_bytes = buf;


  connectionOpen=0;
  ns=-1;
  pfds[0].fd=fd_dvr;
  pfds[0].events=POLLIN|POLLPRI;
  pfds[1].events=POLLIN|POLLPRI;

  while ( !Interrupted) {
    /* Poll the open file descriptors */
    if (ns==-1) {
        poll(pfds,1,500);
    } else {
        pfds[1].fd=ns;  // This can change
        poll(pfds,2,500);
    }


    if (output_type==RTP_TS) {
      /* Attempt to read 188 bytes from /dev/ost/dvr */
      if ((bytes_read = read(fd_dvr,free_bytes,PACKET_SIZE)) > 0) {
        if (bytes_read!=PACKET_SIZE) {
          fprintf(stderr,"No bytes left to read - aborting\n");
          break;
        }

        pid=((free_bytes[1]&0x1f) << 8) | (free_bytes[2]);
        free_bytes[1]=(free_bytes[1]&0xe0)|hi_mappids[pid];
        free_bytes[2]=lo_mappids[pid];
        free_bytes+=bytes_read;

        // If there isn't enough room for 1 more packet, then send it.
        if ((free_bytes+PACKET_SIZE-buf)>MAX_RTP_SIZE) {
          hdr.timestamp = getmsec()*90;
          if (to_stdout) {
            write(1, buf, free_bytes-buf);
          } else {
            sendrtp2(socketOut,&sOut,&hdr,buf,free_bytes-buf);
          }
          free_bytes = buf;
        }
        count++;
      }
    } else if (output_type==RTP_PS) {
       if (read(fd_dvr,buf,TS_SIZE) > 0) {
         my_ts_to_ps((uint8_t*)buf, pids[1], pids[2]);
       } else if(use_stdin) break;
    } else if(output_type==MAP_TS) {
       int bytes_read;
       bytes_read = read(fd_dvr, buf, TS_SIZE);
       if(bytes_read > 0) {
         if(buf[0] == 0x47) {
           int pid, i;

           pid = ((buf[1] & 0x1f) << 8) | buf[2];
           if(getbit(SI_PIDS, pid)) parse_ts_packet(buf);
           if (pids_map != NULL)        {
             for (i = 0; i < map_cnt; i++) {
               if ( ((pids_map[i].start_time==-1) || (pids_map[i].start_time <= now))
		    && ((pids_map[i].end_time==-1) || (pids_map[i].end_time >= now))) {
                 if(getbit(pids_map[i].pidmap, pid)) {
                     errno = 0;
		     if(pids_map[i].filename)
                        write(pids_map[i].fd, buf, TS_SIZE);
		     else {
		        if((pids_map[i].pos + PACKET_SIZE) > MAX_RTP_SIZE) {
        		    hdr.timestamp = getmsec()*90;
		    	    sendrtp2(pids_map[i].socket, &(pids_map[i].sOut), &(pids_map[i].hdr), pids_map[i].buf, pids_map[i].pos);
			    pids_map[i].pos = 0;
			} 
			
			memcpy(&(pids_map[i].buf[pids_map[i].pos]), buf, bytes_read);
			pids_map[i].pos += bytes_read;
		     }
                 }
               }
             }
           }
         } else {
           fprintf(stderr, "NON 0X47\n");
         }
       }
       else if(use_stdin) break;
    } else {
      if (do_analyse) {
        if (read(fd_dvr,buf,TS_SIZE) > 0) {
          pid=((buf[1]&0x1f) << 8) | (buf[2]);
          counts[pid]++;
        }
      }
    }
    if ((secs!=-1) && (secs <=now)) { 
	    
	   // Interrupted=1; 
   
	    gettimeofday(&tv,(struct timezone*) NULL);
	    real_start_time=tv.tv_sec;
	    now=0;
	    
	    secs = interval_secs;

	    renew_output_file();
	        
    }
  }

  if (Interrupted) {
    fprintf(stderr,"Caught signal %d - closing cleanly.\n",Interrupted);
  }

  if (ns!=-1) close(ns);
  close(socketIn);

  if (!to_stdout && !map_cnt) close(socketOut);
  if(!use_stdin) {
    for (i=0;i<npids;i++) close(fd[i]);
    close(fd_dvr);
    close(fd_frontend);
  }

  if (do_analyse) {
    for (i=0;i<8192;i++) {
      if (counts[i]) {
        f=(counts[i]*184.0*8.0)/(secs*1024.0*1024.0);
        if (f >= 1.0) {
          fprintf(stdout,"%d,%.3f Mbit/s\n",i,f);
        } else {
          fprintf(stdout,"%d,%.0f kbit/s\n",i,f*1024);
        }
      }
    }
  }

  return(0);
}
