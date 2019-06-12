//=============================================================================
//
//! \file      mcca.c
//! \par       Title:
//!               Message MIC 
//! \brief     Main Executive for Message MIC
//! \author    Barry Williams
//! \par       Company:
//!               Binghamton University 
//! \date      2016.9.25
//!
//!  Copyright(c) 2016 Binghamton University 
//!  All rights reserved.
//!
//=============================================================================
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// needed for GetTickCount and kbhit
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/times.h>

#include <errno.h>
#include <signal.h>
//#include <bmpfile.h>

//#include <netinet/in.h>
//#include <arpa/inet.h>


#include <pthread.h>
#include <math.h>

#include <mpi.h>


#define MAJOR MAJOR_NUM
#define MINOR MINOR_NUM
#define BUILD BUILD_NUM
#define BUILDDATE BUILD_DATE 

#define RUN_THREAD 1
#define EXIT_THREAD 0

#define FALSE 0
#define TRUE 1

#define RUN_TIME 7

//#define BUFSIZE (1000 * 128)
#define BUFSIZE (2)

#define CIRCBUF_SIZE (256-2)
#define REMBUF_SIZE (16384-1)
#define MAXTHREADS 256
//=============================================================================
//  globals & prototypes
//
//=============================================================================

void *pe_thread(void *arg);
void *rem_thread(void *arg);
//static pthread_mutex_t mt_pe = PTHREAD_MUTEX_INITIALIZER;
static volatile int pe_thread_command = RUN_THREAD;
static volatile int rem_thread_command = RUN_THREAD;

int processing_loop = 1;
long long levents[MAXTHREADS] = {0};
long long tevents[MAXTHREADS] = {0};
long long revents[MAXTHREADS] = {0};
long long rtevents[MAXTHREADS] = {0};
long long rrevents[MAXTHREADS] = {0};

int no_threads = 0;
int ppn = 0;
int epg = 0;
int process = 1, processes = 1;
int machine = 1, machines = 1;

float percent_rthread = 0;
float percent_rprocess = 0;
float percent_remote = 0;

typedef struct payload {
   long process;
   long thread;
} payload;

typedef struct circbuf {
   long long reads;
   long long reserves;
   long long writes;
   long long padding;
   payload message[CIRCBUF_SIZE];
} circbuf;

typedef struct rembuf {
   long long reads;
   long long reserves;
   long long writes;
   long long padding;
   payload dest[REMBUF_SIZE];
   payload message[REMBUF_SIZE];
} rembuf;

__attribute__ ((aligned (64))) circbuf cb[MAXTHREADS];
__attribute__ ((aligned (64))) pthread_mutex_t mt_cb[MAXTHREADS];

__attribute__ ((aligned (64))) rembuf rb;

#ifndef _WIN32
//=============================================================================
///  GetTickCount
//! \brief Elapsed time function
//! \author Barry Williams
//
//=============================================================================


unsigned GetTickCount(void)
{
  struct tms dumbuf;
  unsigned tickspersec = sysconf(_SC_CLK_TCK);
  return (unsigned) (times(&dumbuf)*1000 / tickspersec);
}


//===============================================================
///  getch
//! \brief get a single character from stdin.
//  
//! \details  First stdout is flushed, stdin is then switched to
//! \details  raw mode and input is waited for.  When a single
//! \details  character is received from stdin the tty is restored
//! \details  to its original state and  the character is returned.
//! \details  A conditional allows compiling with or without echo.
//! \author Unknown
//
//===============================================================
                                                                                
int getch(void)
{
  char   ch;
  int   error;
  static struct termios Otty, Ntty;
                                                                                
                                                                                
  fflush(stdout);
  tcgetattr( 0, &Otty);
  Ntty = Otty;
                                                                                
  Ntty.c_iflag   = 0; /* input mode  */
  Ntty.c_oflag   = 0; /* output mode  */
                                                                                
// Change this conditional to enable echo of input character 
#if 1
  Ntty.c_lflag   = 0; // line settings (no echo) 
                                                                                
#else
  Ntty.c_lflag   = ECHO; // line settings (echo) 
#endif
  Ntty.c_cc[VMIN]  = CMIN; // minimum time to wait
#if 0
                                                                                
   // use this to flush the input buffer before blocking for new input

#define FLAG TCSAFLUSH
#else
   
   // use this to return a char from the current input buffer, or block if
   // no input is waiting.
   
#define FLAG TCSANOW
#endif
                                                                                
   if ( 0 == (error = tcsetattr( 0, FLAG, &Ntty)))
   {
       error =  read( 0, &ch, 1 );       // get char from stdin
       error += tcsetattr(0, FLAG, &Otty);   // restore old settings
   }
                                                                                
  return ( error == 1 ? (int) ch : -1 );
}
                                                                                
 
//===============================================================
///  kbhit
//! \brief a keyBOARD lookahead monitor
//
//! \details  returns the number of characters available to read,
//! \details  or -1 on error.
//! \author Unknown
//
//===============================================================

int kbhit(void)
{
   int   cnt = 0;
   int   error;
   static struct termios Otty, Ntty;
                                                                                 
                                                                                 
   tcgetattr( 0, &Otty);
   Ntty = Otty;
                                                                                 
   Ntty.c_iflag   = 0;    // input mode  
   Ntty.c_oflag   = 0;    // output mode  
   Ntty.c_lflag   &= ~ICANON; // raw mode 
   Ntty.c_cc[VMIN]  = CMIN;    // minimum time to wait 
   Ntty.c_cc[VTIME]  = CTIME;   // minimum characters to wait for
                                                                                 
   if (0 == (error = tcsetattr(0, TCSANOW, &Ntty)))
   {
      error += ioctl(0, FIONREAD, &cnt);
      error += tcsetattr(0, TCSANOW, &Otty);
   }
                                                                                 
   return ( error == 0 ? cnt : -1 );
}
#endif  //#ifndef _WIN32


//=============================================================================
///  Debug Print
//
//! \author Barry Williams
//
//=============================================================================

// set default debug modes
static int DebugMsg  = FALSE;

void local_debug_mode(int DebugMsgSw)
{
   DebugMsg = DebugMsgSw;
}

void LogInfo(char *text, ...)
{
   char textbuf[256];
   va_list args;
   va_start(args, text);
   vsprintf(textbuf, text, args);
   va_end(args);
   if(DebugMsg == TRUE)
      printf("%s", textbuf);
}


//=============================================================================
///  pe_cleanup_handler
//
//! \author Barry Williams
//
//=============================================================================

void pe_cleanup_handler(void *arg)
{   
   close(*(int*)arg);
}


//=============================================================================
///  term
//
//! \author Barry Williams
//
//=============================================================================

static int terminate = 0;
void term(int signum)
{
   terminate = signum;
}


//=============================================================================
//  set_cpu_affinity
//
//! \author Barry Williams
//
//=============================================================================


void set_cpu_affinity(int rank){
        int cpuId,status = 0;
        //pthread_t thread;
        cpu_set_t cpuMask;
        //thread = pthread_self();

        CPU_ZERO(&cpuMask);

        if(ppn*(no_threads+1) <= 64)
        {
           //round robin
           cpuId= rank;
        }
        else if (ppn*(no_threads+1) <= 128)
        {
           //round robin
           cpuId= rank/2 + (rank%2)*64;
        }
        else if (ppn*(no_threads+1) <= 192)
        {
           //round robin
           cpuId= rank/3 + (rank%3)*64;
        }
        else if (ppn*(no_threads+1) <= 256)
        {
           //round robin
           cpuId= rank/4 + (rank%4)*64;
        }


        CPU_SET(cpuId, &cpuMask);

        if(CPU_ISSET(cpuId, &cpuMask) >= 0)
        {
           if (sched_setaffinity(0, sizeof(cpuMask), &cpuMask) < 0)
           //status = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuMask);
              if(status !=0){
                 printf("Rank %d, cpuID %d\n", rank, cpuId);
                 perror("NPH: sched_setaffinity");
              }
        }
}



void set_rem_affinity(int rank)
{
    int cpuId;
    cpu_set_t cpuMask;
    CPU_ZERO(&cpuMask);
    cpuId= rank;

    CPU_SET(cpuId, &cpuMask);

    if(CPU_ISSET(cpuId, &cpuMask) >= 0)
    {
       if (sched_setaffinity(0, sizeof(cpuMask), &cpuMask) < 0)
       {
          printf("Rank %d, cpuID %d\n", rank, cpuId);
          perror("NPH: sched_setaffinity");
       }
    }
}


//=============================================================================
//  send_thread
//
//! \author Barry Williams
//
//=============================================================================

int send_thread(int dest_thread, int *buffer)
{
   long long reserves;
   long long reservesnext;
  
   long index;
   
   do
   {
      // wait for room and no pending writes
      while( cb[dest_thread].writes - cb[dest_thread].reads >= CIRCBUF_SIZE 
            || cb[dest_thread].writes != cb[dest_thread].reserves )
      {
          LogInfo("Waiting on destination: %d, reads %lld, reserves %lld, writes %lld\n", 
                 dest_thread, cb[dest_thread].reads, cb[dest_thread].reserves, cb[dest_thread].writes);
      }
      
      reserves = cb[dest_thread].reserves;
      reservesnext = reserves + 1;
   }
   while ( !__sync_bool_compare_and_swap(&(cb[dest_thread].reserves), reserves, reservesnext) );
   
   index = cb[dest_thread].reserves % CIRCBUF_SIZE;

   cb[dest_thread].message[index].process = buffer[0];
   cb[dest_thread].message[index].thread = buffer[1];
   
   return( __sync_bool_compare_and_swap(&(cb[dest_thread].writes), reserves, reservesnext) );
}


//=============================================================================
//  recv_thread
//
//! \author Barry Williams
//
//=============================================================================

int recv_thread(int dest_thread, int *buffer)
{
   long index;

   if( cb[dest_thread].reads < cb[dest_thread].writes )
   {
       index = (cb[dest_thread].reads + 1) % CIRCBUF_SIZE;
       buffer[0] = cb[dest_thread].message[index].process;
       buffer[1] = cb[dest_thread].message[index].thread;
       cb[dest_thread].reads++;
       return 1;
   }
   return 0;
}

//=============================================================================
//  send_remote
//
//! \author Barry Williams
//
//=============================================================================

int send_remote(int dest_proc, int dest_thread, int *buffer)
{
   long long reserves;
   long long reservesnext;
  
   long index;
   
   do
   {
      // wait for room and no pending writes
      while( rb.writes - rb.reads >= REMBUF_SIZE 
            || rb.writes != rb.reserves )
      {
          LogInfo("Waiting on rembuf: reads %lld, reserves %lld, writes %lld\n", 
                  rb.reads, rb.reserves, rb.writes);
      }
      
      reserves = rb.reserves;
      reservesnext = reserves + 1;
   }
   while ( !__sync_bool_compare_and_swap(&(rb.reserves), reserves, reservesnext) );
   
   index = rb.reserves % REMBUF_SIZE;

   rb.dest[index].process = dest_proc;
   rb.dest[index].thread = dest_thread;

   rb.message[index].process = buffer[0];
   rb.message[index].thread = buffer[1];
   
   return( __sync_bool_compare_and_swap(&(rb.writes), reserves, reservesnext) );
}

//=============================================================================
//  recv_remote
//
//! \author Barry Williams
//
//=============================================================================

int recv_remote(int *dest_proc, int *dest_thread, int *buffer)
{
   long index;

   if( rb.reads < rb.writes )
   {
       index = (rb.reads + 1) % REMBUF_SIZE;
       dest_proc[0] = rb.dest[index].process;
       dest_thread[0] = rb.dest[index].thread;
       buffer[0] = rb.message[index].process;
       buffer[1] = rb.message[index].thread;
       rb.reads++;
       return 1;
   }
   return 0;
}


//=============================================================================
//  rem_thread
//
//! \author Barry Williams
//
//=============================================================================

void *rem_thread(void *arg)
{
   int thread = (intptr_t)arg;
   int dest_proc, dest_thread;
   int buffer[BUFSIZE]={0,0};
   int rcvbuf[BUFSIZE+1]={0,0,0};
   int sndbuf[BUFSIZE+1]={0,0,0};
   int flag=0;
   int cpuid;

   unsigned queue = 1;

   //MPI_Request rec_request;
   MPI_Request send_request = 0;
   MPI_Status status;

   cpuid = (process%ppn * (no_threads+1)) + thread;
   //cpuid = thread;
   
   set_cpu_affinity(cpuid);

   //pthread_cleanup_push(pe_cleanup_handler, &connect_skt_cc);
   LogInfo("Remote %d: Waiting...\n", process);  
   while(rem_thread_command != RUN_THREAD);

   LogInfo("Remote %d: Running...\n", process);  
   
   do
   {  
      // check for receive
      if( recv_remote(&dest_proc, &dest_thread, buffer) )
      {
           /*
         if(send_request != 0)
         {
            do
            {
               MPI_Test(&send_request, &flag, &status);
               if(!flag)
                  LogInfo("Remote %d: Still Sending to %d.%d\n", 
                             process, dest_proc, dest_thread);  
            }while( !flag && (pe_thread_command == RUN_THREAD));
         }   */    
         sndbuf[0] = dest_thread;
         sndbuf[1] = buffer[0];
         sndbuf[2] = buffer[1];
         MPI_Isend(sndbuf, BUFSIZE+1, MPI_INT, dest_proc, dest_thread, MPI_COMM_WORLD, &send_request);
        
         queue++;
         LogInfo("Remote %d: Received from %d.%d\n", process, buffer[0], buffer[1] );
      }

      //MPI_Test(&rec_request, &flag, &status);
      MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
      //flag = 0;
      if( flag )
      {
         //MPI_Irecv(buffer, BUFSIZE, MPI_INT, MPI_ANY_SOURCE, thread, MPI_COMM_WORLD, &rec_request);
         MPI_Recv(rcvbuf, BUFSIZE+1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
         dest_thread = rcvbuf[0];
         buffer[0] = rcvbuf[1];
         buffer[1] = rcvbuf[2];
         LogInfo("Remote %d: Received from %d.%d\n", process, buffer[0], buffer[1]);  
         send_thread(dest_thread, buffer);
      }

   }while(rem_thread_command == RUN_THREAD);
   /* 
   usleep(50000);
   do
   {
      MPI_Iprobe(MPI_ANY_SOURCE, thread, MPI_COMM_WORLD, &flag, &status);
      //MPI_Irecv(buffer, BUFSIZE, MPI_INT, MPI_ANY_SOURCE, thread, MPI_COMM_WORLD, &rec_request);
      //MPI_Test(&rec_request, &flag, &status);
      if(flag)
      {
         LogInfo("PE %d.%d: Flushing...\n", process, thread);  
         MPI_Recv(buffer, BUFSIZE, MPI_INT, MPI_ANY_SOURCE, thread, MPI_COMM_WORLD, &status);
         //remote_events++;
      }
   }while( flag );
   */

   LogInfo("Remote %d thread Exiting...\n", process);  
   pthread_exit(0);      
}

//=============================================================================
//  pe_thread
//
//! \author Barry Williams
//
//=============================================================================

void *pe_thread(void *arg)
{
   int thread = (intptr_t)arg;
   long long local_events = 0;
   long long thread_events = 0;
   long long remote_events = 0;
   long long recv_thread_events = 0;
   long long recv_remote_events = 0;
   int dest_machine, dest_proc, dest_thread;
   int buffer[BUFSIZE]={0,0};
   int recbuf[BUFSIZE]={0,0};
   int cpuid;
   unsigned int seed;
   float percent;

   int i;
   volatile double result;
   unsigned queue = 1;

   cpuid = (process%ppn * (no_threads+1)) + thread;
   
   set_cpu_affinity(cpuid);

   //pthread_cleanup_push(pe_cleanup_handler, &connect_skt_cc);
   seed = (process * thread) + 13;
   
   LogInfo("PE %d.%d: Waiting...\n", process, thread);  
   while(pe_thread_command != RUN_THREAD);

   LogInfo("PE %d.%d: Running...\n", process, thread);  
   
   do
   {  
      // synthetic work load
      for(i=0;i<epg;i++){
         result=result + (i*315789)/12345;
         result += 30000;
      }

      
      if(queue > 0)
      {
         // we have a event to process
         queue--;

         percent = rand_r(&seed)/(float)RAND_MAX;

         buffer[0] = process;
         buffer[1] = thread;

         if(percent < percent_remote && machines > 1)
         {
            // remote send
            dest_machine = (rand_r(&seed) % (machines-1)) + 1 + machine;      
            if(dest_machine >= machines) dest_machine = dest_machine - machines;
            if(dest_machine == machine) 
              printf("ERROR, PE %d.%d: sending to my own machine\n", process, thread);

            dest_proc = (rand_r(&seed) % ppn) + dest_machine * ppn;      
            if(dest_proc >= machine * ppn && dest_proc <= machine * ppn + ppn - 1)
               printf("ERROR, PE %d.%d: sending to my own machine\n", process, thread);
            dest_thread = rand_r(&seed) % no_threads;

            LogInfo("Remote machine: PE %d.%d: Sending to %d.%d\n", process, thread, dest_proc, dest_thread);  
            send_remote(dest_proc, dest_thread, buffer);
            /* 
            if(send_request != 0)
            {
               do
               {
                 MPI_Test(&send_request, &flag, &status);
                 if(!flag)
                    LogInfo("PE %d.%d: Still Sending to %d.%d\n", 
                             process, thread, dest_proc, dest_thread);  
               }while( !flag && (pe_thread_command == RUN_THREAD));
            }       
            MPI_Isend(buffer, BUFSIZE, MPI_INT, dest_proc, dest_thread, MPI_COMM_WORLD, &send_request);
            */
            remote_events++;
        
         }
         else if (percent < (percent_remote + percent_rprocess) && processes > 1)
         {
            // this machine, different process
            dest_proc = (rand_r(&seed) % (ppn-1)) + 1 + process;
            if(dest_proc >= ppn) dest_proc = dest_proc - ppn;
            dest_proc = dest_proc + machine * ppn; 
            if(dest_proc == process) 
              printf("Error, PE %d.%d: sending to my own process\n", process, thread);
            dest_thread = rand_r(&seed) % no_threads;
            LogInfo("Remote process: PE %d.%d: Sending to %d.%d\n", process, thread, dest_proc, dest_thread);
            send_remote(dest_proc, dest_thread, buffer);
            remote_events++;
         }
         else if (percent < (percent_remote + percent_rprocess + percent_rthread) && no_threads > 1)
         {
            //this process, different thread
            dest_proc = process;
            dest_thread = rand_r(&seed) % (no_threads-1) + 1 + thread;
            if(dest_thread >= no_threads) dest_thread = dest_thread - no_threads;
            if(dest_thread == thread) 
              printf("Error, PE %d.%d: sending to myself\n", process, thread);
            LogInfo("Remote thread:  PE %d.%d: Sending to %d.%d\n", process, thread, dest_proc, dest_thread);
            if ( !send_thread(dest_thread, buffer) )
               printf("PE %d.%d: Sending to %d.%d FAILED\n", process, thread, dest_proc, dest_thread);
            else
               thread_events++;
         }
         else
         {
            // this thread 
            local_events++;
            queue++;
         }
      }

      // check for receive
      while( recv_thread(thread, recbuf) )
      {
         queue++;
         LogInfo("PE %d.%d: received message from %d.%d\n", process, thread, recbuf[0], recbuf[1] );
         if(recbuf[0] != process)
           recv_remote_events++;
         else if (recbuf[1] != thread)
           recv_thread_events++;
         else
           printf("Error PE %d.%d: received remote message from myself %d.%d\n", process, thread, recbuf[0], recbuf[1] );
      }

      //pthread_mutex_lock(&mt_cb[thread]);
      //if(cb[thread].writes > cb[thread].reads)
      //   printf("Message received by thread %d\n", thread);
      //pthread_mutex_unlock(&mt_cb[thread]);
      //for(i=0; i<no_threads; i++)
      //{
      //   if(cb[thread][i].writes > cb[thread][i].reads)
      //      printf("Message received from thread %d\n", i);
      //}

      //MPI_Test(&rec_request, &flag, &status);
      //MPI_Iprobe(MPI_ANY_SOURCE, thread, MPI_COMM_WORLD, &flag, &status);
      //flag = 0;
      //if( flag )
      //{
         //MPI_Irecv(buffer, BUFSIZE, MPI_INT, MPI_ANY_SOURCE, thread, MPI_COMM_WORLD, &rec_request);
      //   MPI_Recv(buffer, BUFSIZE, MPI_INT, MPI_ANY_SOURCE, thread, MPI_COMM_WORLD, &status);
      //   queue++;
      //   LogInfo("PE %d.%d: Received from %d.%d\n", process, thread, buffer[0], buffer[1]);  
      //}

   }while(pe_thread_command == RUN_THREAD);

   usleep(50000);
   // flush the pipe   
   while( recv_thread(thread, recbuf) )
   {
      queue++;
      LogInfo("PE %d.%d: received message from %d.%d\n", process, thread, recbuf[0], recbuf[1] );
      if(recbuf[0] != process)
        recv_remote_events++;
      else if (recbuf[1] != thread)
        recv_thread_events++;
      else
        printf("Error PE %d.%d: received remote message from myself %d.%d\n", process, thread, recbuf[0], recbuf[1] );
   }

   /* 
   do
   {
      MPI_Iprobe(MPI_ANY_SOURCE, thread, MPI_COMM_WORLD, &flag, &status);
      //MPI_Irecv(buffer, BUFSIZE, MPI_INT, MPI_ANY_SOURCE, thread, MPI_COMM_WORLD, &rec_request);
      //MPI_Test(&rec_request, &flag, &status);
      if(flag)
      {
         LogInfo("PE %d.%d: Flushing...\n", process, thread);  
         MPI_Recv(buffer, BUFSIZE, MPI_INT, MPI_ANY_SOURCE, thread, MPI_COMM_WORLD, &status);
         //remote_events++;
      }
   }while( flag );
   */
   levents[thread] = local_events;
   tevents[thread] = thread_events;
   revents[thread] = remote_events;
   rtevents[thread] = recv_thread_events;
   rrevents[thread] = recv_remote_events;

   LogInfo("PE %d.%d: Exiting...\n", process, thread);  
   pthread_exit(0);      
}


//=============================================================================
///  main
//
//! \author Barry Williams
//
//=============================================================================

int main(int argc, char *argv[])
{
   FILE *fp;
   char filename[256];
   int result;
   int thread;
   //int proc;
   int loop = 0;
   long long total_events = 0, lcount = 0, tcount = 1, rcount = 1, proc_lcount = 0, proc_tcount = 0, proc_rcount = 0;
   long long rtcount = 1, rrcount = 1, proc_rtcount = 0, proc_rrcount = 0;
   double event_rate;
   
   unsigned start_time, end_time;
   
   int ierr;
   //MPI_Status mpistat;
   int provided;

   int i;
   
   printf("payload is %ld bytes, cirbuf is %ld bytes, cb is %ld bytes\n", sizeof(payload), sizeof(circbuf), sizeof(cb));
   printf("rembuf is %ld bytes, rb is %ld bytes\n", sizeof(rembuf), sizeof(rb));
   
   if(argc < 7 || argc > 8)
   {
      printf("usage: mcca threads ppn percent_rthread percent_rprocess percent_remote epg -debug (optional)\n");
      return -1;
   }

   if(sizeof(circbuf) % 64 != 0)
      printf("  Warning - circular buffer is not cache aligned at %ld bytes!!!\n", sizeof(circbuf));
   
   if(sizeof(rembuf) % 64 != 0)
      printf("  Warning - remote buffer is not cache aligned at %ld bytes!!!\n", sizeof(rembuf));

   char * num_t = argv[1];
   no_threads = atoi(num_t);
   argc--;

   char * num_p = argv[2];
   ppn = atoi(num_p);
   //real_no_threads = no_threads;
   argc--;

   char * per = argv[3];
   percent_rthread = atof(per);
   argc--;

   char * thrd = argv[4];
   percent_rprocess = atof(thrd);
   argc--;

   char * reg = argv[5];
   percent_remote = atof(reg);
   argc--;

   char * _epg = argv[6];
   epg = atoi(_epg);
   argc--;

   if(argc > 1)
      local_debug_mode(TRUE);
   else
      local_debug_mode(FALSE);

   if(signal(SIGINT, term) == SIG_ERR)
      printf("Cannot catch SIGINT\n");
  
   //ierr = MPI_Init(&argc, &argv);
   ierr = MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
   if(ierr)
   {
      printf("ierr:MPI_Init_thread\n");
      return -1;
   }

   if(provided != MPI_THREAD_FUNNELED)
   {
      printf("Threading not supported\n");
      return -1;
   }

   ierr = MPI_Comm_rank(MPI_COMM_WORLD, &process);
   if(ierr)
   {
      printf("ierr:MPI_Comm_rank\n");
      return -1;
   }

   ierr = MPI_Comm_size(MPI_COMM_WORLD, &processes);
   if(ierr)
   {
      printf("ierr:MPI_Comm_size\n");
      return -1;
   }

   //sscanf(argv[1],"%d",&no_threads);
   //sscanf(argv[2],"%f",&percent_remote);

   if(processes * no_threads <= 1)
   {
      printf("It takes at least two to tango\n");
      return -1;
   }
   
   machine = process/ppn;
   machines = processes/ppn;

   if(process == 0)
   {
      printf("Message MIC Version %d.%d.%d - %d\n",
              MAJOR, MINOR, BUILD, BUILDDATE);
  
      printf("%d Total MPI Processes, %d Machines, %d Processes per Machine, %d Threads per Process\n",
              processes, machines, ppn, no_threads);
   }
   
   sprintf(filename,"%d_%d_%d_%0.4f_%0.4f_%0.4f_results.txt", processes, ppn, no_threads, 
           percent_rthread, percent_rprocess, percent_remote);
   
   pthread_t pe_id[no_threads];
   pthread_t rem_id;

   pe_thread_command = EXIT_THREAD;
   rem_thread_command = EXIT_THREAD;

   for(i=0; i < MAXTHREADS; i++)
   {
      cb[i].reads = cb[i].writes = 0;
      pthread_mutex_init(&mt_cb[i], NULL);
   }

   for(thread = 0; thread < no_threads; thread++)
   {
      result = pthread_create (&(pe_id[thread]), NULL, pe_thread, (void *)(intptr_t)thread);
      if (result != 0)
      {
         printf(" Create PE %d.%d failed, status = %d\n",
                process, thread, result);
         exit(0);
      }
   }

   printf("Creating %d threads in process %d\n", thread, process);
   printf("Creating Remote %d\n", process);
   result = pthread_create (&rem_id, NULL, rem_thread, (void *)(intptr_t)thread);
   if (result != 0)
   {
      printf(" Create Remote %d failed, status = %d\n",
             process, result);
      exit(0);
   }

   MPI_Barrier(MPI_COMM_WORLD);

   start_time = GetTickCount();

   rem_thread_command = RUN_THREAD;
   pe_thread_command = RUN_THREAD;
   
   while(!terminate && loop < RUN_TIME)
   {
      sleep(1);
      loop++;
   }

   if(terminate)
      LogInfo("Received Signal %d, exiting...\n", terminate);
   
   pe_thread_command = EXIT_THREAD;
 
   // delay to allow messages to all be cleared
   usleep(100000);
   rem_thread_command = EXIT_THREAD;

   printf("Process %d beginning to exit\n", process );
   //end_time = GetTickCount();
   
   for(thread = 0; thread < no_threads; thread++)
   {
      result = pthread_join(pe_id[thread], NULL);
      if (result != 0)
         printf("Join PE thread failed, status = %d\n", result);
   }
   
   result = pthread_join(rem_id, NULL);
      if (result != 0)
         printf("Join Remote thread failed, status = %d\n", result);
   end_time = GetTickCount();

   MPI_Barrier(MPI_COMM_WORLD);

   for(thread = 0; thread < no_threads; thread++)
   {
      proc_lcount += levents[thread];
      proc_tcount += tevents[thread];
      proc_rcount += revents[thread];
      proc_rtcount += rtevents[thread];
      proc_rrcount += rrevents[thread];
   }

   printf("Process %d: %lld local events, %lld thread events, %lld remote events\n",
          process, proc_lcount, proc_tcount, proc_rcount );

   MPI_Allreduce(&proc_lcount, &lcount, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
   MPI_Allreduce(&proc_tcount, &tcount, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
   MPI_Allreduce(&proc_rcount, &rcount, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
   MPI_Allreduce(&proc_rtcount, &rtcount, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
   MPI_Allreduce(&proc_rrcount, &rrcount, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);


   if(process == 0)
   {
      /*
      for(proc = 1; proc < processes; proc++)
      {

         ierr = MPI_Recv(&proc_count, 1, MPI_LONG_LONG,
                         proc, MPI_ANY_TAG, MPI_COMM_WORLD, &mpistat);
       
         printf("Received Message from %d\n", proc);
         count += proc_count; 
      
      }

      end_time = GetTickCount();
   
      for(thread = 0; thread < no_threads; thread++)
         count += events[thread];
      */ 

      if(rtcount != tcount)
         printf("Error: Thread remote count missmatch.  Sent %lld, Received %lld\n",
                 tcount, rtcount);

      if(rrcount != rcount)
         printf("Error: Process remote count missmatch.  Sent %lld, Received %lld\n",
                 rcount, rrcount);

      total_events = lcount + tcount + rcount;
      event_rate = total_events / (((double)(end_time - start_time))/1000.0);
   
      printf("Start Ticks %d, End Ticks %d, Elapsed Time =  %0.2lf\n",
            start_time, end_time, ((double)(end_time - start_time))/1000.0 );
   
   
      printf("%d Processes, %d Threads, %lld events, %0.2f%% thread, %0.2f%% remote, %0.2lf events per second\n",
             processes, no_threads, total_events, 
              (float)tcount/(float)total_events*100, (float)rcount/(float)total_events*100, event_rate );
      //printf("Process %d, %d Threads, %lld events, %0.2lf events per second\n", 
      //process, no_threads, count, event_rate );

      fp = fopen(filename, "w");
      fprintf(fp, "%d, %d, %d, %0.4f, %0.4f, %0.4f, %lld, %0.2lf\n", 
              processes/ppn, ppn, no_threads, percent_rthread, percent_rprocess, percent_remote, total_events, event_rate );
      fclose(fp);
   }
/*   else
   {
      end_time = GetTickCount();
   
      for(thread = 0; thread < no_threads; thread++)
         count += events[thread];

      ierr = MPI_Send(&count, 1, MPI_LONG_LONG, 0, 0, MPI_COMM_WORLD);
   }
*/
   ierr = MPI_Finalize();
   
   return 0;
}
