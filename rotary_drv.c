/*
	 Rotary driver - (C) 2015 Massimiliano Frigieri

	 This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>

#define DRIVER_AUTHOR "Massimiliano Frigieri>"
#define DRIVER_DESC   "Rotary driver"
 
// we want GPIO_1 (pin 5 on pinout raspberry pi rev. 1 board)
// to generate interrupt; we chose this pin because it has an
// internal pull-up resistor
#define GPIO_ANY_GPIO                1

 
// text below will be seen in 'cat /proc/interrupt' command
#define GPIO_ANY_GPIO_DESC           "Rotary interrupt on GPIO pin 5"
 
// below is optional, used in more complex code, in our case, this could be
// NULL
#define GPIO_ANY_GPIO_DEVICE_DESC    "rotary_device"

#define DEBOUNCE_MILLI (70)
static unsigned long  TIMEOUT_DELAY = (HZ / 2);
static unsigned long  ENBLOC_DELAY =  (HZ * 3);

/****************************************************************************/
/* Module parameters block                                                  */
/****************************************************************************/

#define MAXIMUM_NUMBER_LEN 60
static int max_number_len = 20;

module_param(max_number_len, int, S_IRUGO);
MODULE_PARM_DESC(max_number_len, "Maximum composed number length");

/****************************************************************************/
/* Interrupts variables block                                               */
/****************************************************************************/
short int irq_any_gpio    = 0;

// characters written inside file
static char *ticks_output_buffer;
static int  ticks_output_buffer_len = 0;
static int  ticks_count = 0;

// driver identifier
static int 	major; 

// Debouncing helpers
static uint64_t last_interrupt_time = 0;
 
static struct timer_list ticks_timer;	/* Timer for the end of a digit     */ 
static struct timer_list sequence_timer;/* Timer for the end of a sequence  */ 

static DECLARE_WAIT_QUEUE_HEAD(ticks_wait_queue); /* Used for blocking read */

/****************************************************************************/
/* Debouncing                                                               */
/****************************************************************************/

unsigned int millis (void)
{
  uint64_t now ;
  static uint64_t 		epochMilli = 0;

  now = sched_clock();
  do_div(now, 1000000);
  if(!epochMilli)
      epochMilli = now;

  return (uint32_t)(now - epochMilli) ;
}

static void sequence_sequence_finished (unsigned long parameters)
{
    ticks_output_buffer[ticks_output_buffer_len++] = '\n';
    ticks_output_buffer[ticks_output_buffer_len++] = '\0';
    
    printk("Rotary driver: buffer=%s\n",ticks_output_buffer);
    ticks_output_buffer_len = 0;
    
    wake_up_interruptible (&ticks_wait_queue);

}

static void ticks_sequence_finished (unsigned long parameters)
{

    if (ticks_output_buffer_len < max_number_len)
        ticks_output_buffer[ticks_output_buffer_len++] = '0' + ticks_count%10;
    
	ticks_count = 0;		/* Reset the button press counter */
    
    // No more room, send out the buffer
    if (ticks_output_buffer_len >= max_number_len)
    {
        del_timer(&sequence_timer);
        sequence_sequence_finished(0);
    }
    else if (ticks_output_buffer_len == 1)
    {
        // Arm end of sequence timer
        init_timer (&sequence_timer);
        sequence_timer.function = sequence_sequence_finished;
        sequence_timer.expires = (jiffies + ENBLOC_DELAY);
        add_timer (&sequence_timer);
   }
   else
   {
        // Update end of sequence timer 
        mod_timer(&sequence_timer, jiffies + ENBLOC_DELAY);
   }
}

/****************************************************************************/
/* IRQ handler - fired on interrupt                                         */
/****************************************************************************/
static irqreturn_t r_irq_handler(int irq, void *dev_id, struct pt_regs *regs) 
{

   uint64_t curr_int_time = millis();
   // Debounce
   if (last_interrupt_time && curr_int_time - last_interrupt_time < DEBOUNCE_MILLI) 
   {
    return IRQ_HANDLED;
   }

   last_interrupt_time = curr_int_time;

   if (!ticks_count)
   {
     init_timer (&ticks_timer);
     ticks_timer.function = ticks_sequence_finished;
     ticks_timer.expires = (jiffies + TIMEOUT_DELAY);
     add_timer (&ticks_timer);
   }
   else
   {
     mod_timer(&ticks_timer, jiffies + TIMEOUT_DELAY);
   }

   ticks_count++;
   
   return IRQ_HANDLED;
}
 
/****************************************************************************/
/* This function configures interrupts.                                     */
/****************************************************************************/
bool r_int_config(void) {
 
   if (gpio_request(GPIO_ANY_GPIO, GPIO_ANY_GPIO_DESC)) {
      printk("GPIO request failure: %s\n", GPIO_ANY_GPIO_DESC);
   }
 
   if ( (irq_any_gpio = gpio_to_irq(GPIO_ANY_GPIO)) < 0 ) {
      printk("GPIO to IRQ mapping failure %s\n", GPIO_ANY_GPIO_DESC);
      return false;
   }
 
   printk(KERN_NOTICE "Rotary driver: mapped int %d\n", irq_any_gpio);
 
   if (request_irq(irq_any_gpio,
                   (irq_handler_t ) r_irq_handler,
                   IRQF_TRIGGER_RISING,
                   GPIO_ANY_GPIO_DESC,
                   GPIO_ANY_GPIO_DEVICE_DESC)) {
      printk("Rotary driver: IRQ Request failure\n");
      return false;
   }
 
   return true;
}
 
 
/****************************************************************************/
/* This function releases interrupts.                                       */
/****************************************************************************/
void r_int_release(void) {
 
   free_irq(irq_any_gpio, GPIO_ANY_GPIO_DEVICE_DESC);
   gpio_free(GPIO_ANY_GPIO);
 
   return;
}
 
 
/****************************************************************************/
/* Character device setup/teardown.                                         */
/****************************************************************************/

static ssize_t device_read(struct file *filp, char __user *buffer, size_t length, loff_t *offset)
{
    unsigned int len = 0;
	interruptible_sleep_on (&ticks_wait_queue);
    len = strlen(ticks_output_buffer);
    return (copy_to_user (buffer, ticks_output_buffer, len))
		 ? -EFAULT : len;
}

static struct file_operations fops = 
{
	.owner =	THIS_MODULE,
	.read  =	device_read
};

int device_config(void)
{
	major = register_chrdev(0, "rotary_device", &fops);
	if (major < 0) 
	{
		printk ("Registering the character device failed with %d\n", major);
		return major;
	}
	return 0;

}

int device_release(void)
{
	unregister_chrdev(major, "rotary_device");
	return 0;
}

/****************************************************************************/
/* Module init / cleanup block.                                             */
/****************************************************************************/
int r_init(void) {
 
   //Set up epoch time
   millis();
   printk(KERN_NOTICE "Rotary driver initialization...\n");

   if (r_int_config())
   {
       if(device_config() >= 0)
       {
           // Create buffer of the specified size + size for \n\0
           if (max_number_len > MAXIMUM_NUMBER_LEN)
           {
               printk(KERN_NOTICE "Specified a maximum number length greater than %d, clamped.\n",
                      MAXIMUM_NUMBER_LEN);
               max_number_len = MAXIMUM_NUMBER_LEN;
           }
           ticks_output_buffer = (char *)kmalloc((max_number_len + 4) * sizeof(char), GFP_KERNEL);
           if (ticks_output_buffer != NULL)
               return 0;
       }
   }

   return -1;
}
 
void r_cleanup(void) {
   printk(KERN_NOTICE "Rotary driver finalization...\n");
   r_int_release();
   device_release();
   kfree(ticks_output_buffer);
   return;
}
 
module_init(r_init);
module_exit(r_cleanup);
 
/****************************************************************************/
/* Module licensing/description block.                                      */
/****************************************************************************/
MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
