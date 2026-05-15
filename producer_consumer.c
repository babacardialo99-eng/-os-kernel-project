#include <linux/init.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/tty.h>
#include <linux/uidgid.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/string.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <linux/timer.h>
#include <linux/ktime.h>
#include <linux/time_namespace.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Babacar Diallo");
MODULE_DESCRIPTION("CSE330 Spring 2026 Project (Process Management)\n");
MODULE_VERSION("0.1");


#define MAX_BUFFER_SIZE 500
#define MAX_NO_OF_PRODUCERS 1
#define MAX_NO_OF_CONSUMERS 100
#define PCINFO(s, ...) pr_info("###[%s]###" s, __FUNCTION__, ##__VA_ARGS__)

unsigned long long total_time_elapsed = 0;
int producer_thread_function(void *pv);
int consumer_thread_function(void *pv);
char *replace_char(char *str, char find, char replace);
void name_threads(void);
// use this struct to store the process information
struct process_info {

	unsigned long pid; // WHO
	unsigned long long start_time; // WHEN THE process started
	unsigned long long boot_time;  // WHEN we measured it.
} process_default_info = {0, 0, 0}; // 3 zero because process has 3 fields. 

int total_no_of_process_produced = 0;
int total_no_of_process_consumed = 0;

int end_flag = 0;

char producers[MAX_NO_OF_PRODUCERS][12] = {"kProducer-X"};
char consumers[MAX_NO_OF_CONSUMERS][12] = {"kConsumer-X"};

static struct task_struct *ctx_producer_thread[MAX_NO_OF_PRODUCERS];
static struct task_struct *ctx_consumer_thread[MAX_NO_OF_CONSUMERS];

// use fill and use to keep track of the buffer
struct process_info buffer[MAX_BUFFER_SIZE];
int fill = 0;  // where PRODUCER writes next
int use  = 0;  // where CONSUMER reads  next

// Variables to manage the buffer
int buffSize = 0;   // Current buffer size
int prod = 0;       // Number of producer threads
int cons = 0;       // Number of consumer threads
int uuid = 0;       // Unique identifier

// TODO: use module_param to pass them from insmod command line. (--Project 1)
// Allows the user (via insmod / test.sh) to specify
// how many slots the shared producer–consumer buffer has
module_param(buffSize, int, 0);

// Allows the user to specify how many producer kernel threads
// should be created when the module is loaded
module_param(prod, int, 0);

// Allows the user to specify how many consumer kernel threads
// should be created when the module is loaded
module_param(cons, int, 0);

// Allows the user to specify the UID whose processes
// will be scanned and timed by the producer thread
module_param(uuid, int, 0);

struct semaphore empty;
struct semaphore full;
struct semaphore mutex;

// Producer kernel thread:
// - Scans the kernel task list ONCE
// - For each process owned by UID == uuid, it "produces" one item into the shared buffer
// - Then exits after it finishes the scan (matches assignment spec)
int producer_thread_function(void *pv) {

    allow_signal(SIGKILL);          // Allow this thread to be killed by a signal
    struct task_struct *task;       // Used by for_each_process() to walk the process list

    // Walk every process in the system exactly once
    for_each_process(task)  {
   
        // task->cred->uid.val is the UID of the user that owns this process
        // If this process is NOT owned by uuid, skip it
        if (task->cred->uid.val != uuid)
            continue;

        // =========================
        // 1) Wait for an empty slot
        // =========================
        // empty counts how many EMPTY buffer slots are available.
        // If buffer is full, producer sleeps here until a consumer frees a slot.
        down(&empty);

        // Safe exit point AFTER we might have slept
        if (kthread_should_stop()) {
        
            up(&empty);             // Give back the empty slot reservation
            break;                  // Exit the producer thread
        }

        // =========================
        // 2) Lock the shared buffer
        // =========================
        // mutex makes sure only one thread edits buffer[] / fill at a time.
        down(&mutex);

        // Safe exit point AFTER taking the mutex
        if (kthread_should_stop())    {
     
            up(&mutex);             // Unlock buffer
            up(&empty);             // Give back empty-slot reservation
            break;                  // Exit the producer thread
        }

    // =========================
    // 3) PRODUCE 1 ITEM
    // =========================
    // We are using the process_info struct (buffer[fill] is a process_info).
    // The assignment says consumers need: pid, start_time, boot_time.
    buffer[fill].pid        = task->pid;         
    // store start time as seconds (integer)
    buffer[fill].start_time = task->start_time;   // if your kernel defines it as ns already

        // Save where we wrote (nice for printing)
        // int produced_index = fill; commented for now

        // Move fill forward (circular buffer)
        fill = fill + 1;
        if (fill == buffSize)
            fill = 0;

        // Count produced items
        total_no_of_process_produced++;
/*
        // Print debug message
         PCINFO("[%s] Produce-Item#:%d at buffer index:%d for PID:%d\n",
               current->comm,
             total_no_of_process_produced,
               produced_index,
               task->pid);
 */
        // =========================
        // 4) Unlock + signal "full"
        // =========================
        up(&mutex);                 // Unlock buffer
        up(&full);                  // Tell consumers: "1 item is available"
    }

    PCINFO("[%s] Producer Thread stopped.\n", current->comm);
    ctx_producer_thread[0] = NULL;   // mark producer as stopped
    return 0;
}

/***************************************************************************************/


	// Consumer kernel thread:
// - Waits for "full" (an available produced item)
// - Takes mutex to safely read from buffer[use]
// - Releases mutex and signals "empty" (one free slot)
// - Updates total_time_elapsed (in ns)
int consumer_thread_function(void *pv) {
    allow_signal(SIGKILL);

    int no_of_process_consumed = 0;   // local counter for THIS consumer thread
    struct process_info item;         // holds ONE consumed item

    while (!kthread_should_stop())  {
  
        // Wait for an available produced item
        down(&full);

        // If module is exiting, undo and stop
        if (end_flag || kthread_should_stop()) {
        
            up(&full);
            break;
        }
    
        // Lock buffer so only one thread touches it
        down(&mutex);

        // Safe exit after locking
        if (end_flag || kthread_should_stop()) {
        
            up(&mutex);
            up(&full);
            break;
        }
 
       // Take ONE item from buffer
item = buffer[use];

// Move to next read slot (circular)
use = use + 1;
if (use == buffSize)
    use = 0;

// Update counters (shared) while still holding mutex
no_of_process_consumed++;
total_no_of_process_consumed++;

// Add elapsed time (shared) while still holding mutex
 
    // get current boot-time in seconds (integer)
 unsigned long long now = ktime_get_boottime_ns();

 if (now > item.start_time)
    total_time_elapsed += (now - item.start_time);

// Unlock + signal one empty slot is free now
up(&mutex);
up(&empty);
        // Simple print (beginner friendly)
       // PCINFO("Consumed item #%d, PID=%lu\n", no_of_process_consumed, item.pid);  // commented for now
    }

    PCINFO("Consumer Thread stopped.\n");
    return 0;
}

char *replace_char(char *str, char find, char replace) {
	char *current_pos = strchr(str, find);
	while (current_pos) {
		*current_pos = replace;
		current_pos = strchr(current_pos, find);
	}
	return str;
 }

void name_threads(void) {
	for (int index = 0; index < prod; index++) {
		char id = (index + 1) + '0';
		strcpy(producers[index], "kProducer-X");
		strcpy(producers[index], replace_char(producers[index], 'X', id));
	}

	for (int index = 0; index < cons; index++) {
		char id = (index + 1) + '0';
		strcpy(consumers[index], "kConsumer-X");
		strcpy(consumers[index], replace_char(consumers[index], 'X', id));
	 }
  }

  static int __init thread_init_module(void) {

	PCINFO("CSE330 Project Kernel Module Inserted\n");
	PCINFO("Kernel module received the following inputs: UID:%d, Buffer-Size:%d, No of Producer:%d, No of Consumer:%d", uuid, buffSize, prod, cons);

	if (buffSize > 0 && (prod >= 0 && prod < 2)) {

		
   sema_init(&empty, buffSize);  // empty slots available at start
   sema_init(&full, 0);          // no items produced yet
   sema_init(&mutex, 1);         // lock for buffer (1 = unlocked)
   name_threads();

		for (int index = 0; index < buffSize; index++)
			buffer[index] = process_default_info;

		// TODO: use kthread_run to create producer kernel threads here
		if (prod == 1) {
			ctx_producer_thread[0] =
			// Start producer thread: runs producer_thread_function(NULL) and names it producers[0]
			  kthread_run (producer_thread_function, NULL, producers[0]); 
		 }

		
		// TODO: use kthread_run to create consumer kernel threads here
		for (int i = 0; i < cons; i++) {
			ctx_consumer_thread [i] = 
			kthread_run(consumer_thread_function, NULL, consumers[i]);

		}
		// Hint: Please refer to sample code to see how to use kthread_run, kthread_should_stop, kthread_stop, etc.
		// Hint: use ctx_consumer_thread[index] to store the return value of kthread_run
		
	  }
	  else {
	
		// Input Validation Failed
		PCINFO("Incorrect Input Parameter Configuration Received. No kernel threads started. Please check input parameters.");
		PCINFO("The kernel module expects buffer size (a positive number) and # of producers(0 or 1) and # of consumers > 0");
	 }

	return 0;
  }

  static void __exit thread_exit_module(void) {

    if (buffSize > 0) {

        end_flag = 1;   // tell consumers to stop

        // Wake up threads that may be sleeping so they can exit
        if (prod)
            up(&empty);          // wake producer if blocked on empty

        for (int i = 0; i < cons; i++)
            up(&full);           // wake consumers if blocked on full

        // Stop producer
        for (int i = 0; i < prod; i++) {
            if (ctx_producer_thread[i])
                kthread_stop(ctx_producer_thread[i]);
        }

        // Stop consumers
        for (int i = 0; i < cons; i++) {
            if (ctx_consumer_thread[i])
                kthread_stop(ctx_consumer_thread[i]);
        }

        //  kernel total_time_elapsed is now in nsec
        total_time_elapsed = total_time_elapsed / 1000000000;

        unsigned long long total_time_hr = total_time_elapsed / 3600;
        unsigned long long total_time_min = (total_time_elapsed - 3600 * total_time_hr) / 60;
        unsigned long long total_time_sec = (total_time_elapsed - 3600 * total_time_hr) - (total_time_min * 60);

        PCINFO("Total number of items produced: %d", total_no_of_process_produced);
        PCINFO("Total number of items consumed: %d", total_no_of_process_consumed);
        PCINFO("The total elapsed time of all processes for UID %d is \t%llu:%llu:%llu  \n",
               uuid, total_time_hr, total_time_min, total_time_sec);
     }

    PCINFO("CSE330 Project Kernel Module Removed\n");
}

module_init(thread_init_module);
module_exit(thread_exit_module);

