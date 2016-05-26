/**
* author: liulei2010@ict.ac.cn
* @20130629
* sequentially scan the page table to check and re-new __access_bit, and cal. the number of hot pages. 
* add shadow count index
*
* Modifications@20150130 recover from an unknown problem. This version works well.
* 
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/sem.h>
#include <linux/list.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/module.h>
#include <linux/delayacct.h>
#include <linux/init.h>
#include <linux/writeback.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/kallsyms.h>
#include <linux/swapops.h>
#include <linux/elf.h>
#include <linux/sched.h>

struct timer_list stimer; 
static int scan_pgtable(void);
static struct task_struct * traver_all_process(void);
long long shadow1[300000];

//write by yanghao
int page_counts;//to record number of used page.
int reuse_time[200];//to record each 200 loops the reuse_distance of one page.
int random_page;//the No. of page being monitored.
int sampling_interval;//random_page's sampling interval.
int dirty_page[200];//to record the writting informations about random_page.
int read_times[300000];//the reading times of each page.
int write_times[300000];//the writting times of each page.
int out_data[300000];
int w2r,r2w;
int history[400][300000];//the history of RD and WD 
int loops;
int page_read_times[300000];
int page_write_times[300000];
int highr_yanghao,highw_yanghao,midhigh_yanghao,mid_yanghao,midlow_yanghao;
//end
static int process_id;
module_param(process_id, int, S_IRUGO|S_IWUSR);

/**
 * liulei2010
 * NOTE: You are expected to modify the number of banks according to detail configrations of DRAM systems.
 */
#define BANK_NUM 32

/**
 * liulei2010@ict.ac.cn
 * function: Get the memory mapping infor, and the distribution status at DRAM bank level. 
 * date:20150324 
 */
/*
//0~31 slots, and each slot in bank_map[] denotes a DRAM bank (specific color)
unsigned long bank_map[BANK_NUM], bank_map_hotpage[BANK_NUM], bank_acc_count[BANK_NUM];
//denotes the bank index for each pages (i.e, hot pages), the information should be used to calculate the bank balance factor. 
//unsigned int bank_index_shadow[300000];
*/

//NUMA
unsigned long bank_map[BANK_NUM], bank_map_hotpage[BANK_NUM], bank_acc_count[BANK_NUM];
unsigned int bank_index_shadow[300000];
/**
 * liulei2010
 * i7-860, 8GB memory, 32 DRAM banks/per-channel, 2 channels
 */

#define L2_CACHE_SIZE (4096*1024)
#define CACHE_COLOR_SHIFT 12
#define NUM_CACHE_COLOR_BITS 5 // 5 bits for entirely coloring
#define NUM_CACHE_COLOR (1<<NUM_CACHE_COLOR_BITS)
#define BANK_COLOR (0x600) //21,22 bits
#define CACHE_COLOR (0xe)  //13,14,15 bits
//get the bank id
#define PAGE_TO_COLOR(page) ((page_to_pfn(page) & (BANK_COLOR))>>6 | (page_to_pfn(page) & (CACHE_COLOR))>>1)

//micro-define for identity of XOR used bank mapping
/**
 * liulei2010
 * date:20150417
 * function: get target color for a specific physical page with XOR policy
 * mapping: 23 22 | 21 20 | 19 18 || 17 16 | 15 14 13 12
 * bank bits with XOR: b1:14xor18, b2:15xor19, b3:16xor20, b4:17xor21
 * platform: i3-2120. 8GB memory, 16 DRAM banks/per-channel, 2 channels
 */
/*
#define XOR1 (0xc)  //14~15 bits
#define XOR2 (0x30)  //16~17 bits
#define XOR3 (0xc0)  //18~19 bits
#define XOR4 (0x300)  //20~21 bits
//get the xor bits
#define XOR_LEFT(page) ((page_to_pfn(page)&(XOR2))>>2 | (page_to_pfn(page)&(XOR1))>>2)  //14,15,16,17 bits
#define XOR_RIGHT(page) ((page_to_pfn(page)&(XOR4))>>6 | (page_to_pfn(page)&(XOR3))>>6)   //18,19,20,21 bits
//get the bank id
//#define PAGE_TO_COLOR(page) XOR_LEFT(page) ^ XOR_RIGHT(page) //4 bits denote bank id
*/

/**
 * liulei2010
 * date:20150429
 * function: get target color for a specific physical page with XOR policy
 * mapping:
 * bank bits with XOR: b1:14xor19, b2:15xor20, b3:17xor21, b4:18xor22, b5:16. 
 * platform: i3-2120. 8GB memory, 125MB/bank, 32 DRAM banks/per-channel, 2 channels
 */
//#define XOR_14_15 (0xc)
//#define XOR_17_18 (0x60)
//#define XOR_19 (0x80)
//#define XOR_20 (0x100)
//#define XOR_21_22 (0x600)
//#define BIT_16 (0x10)
//get the XOR bits
/*
#define XOR_RIGHT(page) (((page_to_pfn(page)&(0xc))>>2) ^ ((page_to_pfn(page)&(0x80))>>7 | (page_to_pfn(page)&(0x100))>>7))   
#define XOR_LEFT(page) (((page_to_pfn(page)&(0x60))>>2) ^ ((page_to_pfn(page)&(0x600))>>6))
#define XOR_MID(page) ((page_to_pfn(page)&(0x10))>>2)
//get the bank id
#define PAGE_TO_COLOR(page) ((XOR_RIGHT(page)) | (XOR_MID(page)) | (XOR_LEFT(page)))
*/
/**
 * liulei2010
 * date: 20150423
 * function: get the bank index
 * bank bits mapping: 21,20,14,13
 * platform: e5600 X 2 in NUMA architecture; 32GB memory; 16GB memory/per-cpu
 */
/*
 * #define BANK_COLOR1 (0x6)
 * #define BANK_COLOR2 (0x300)
 * #define PAGE_TO_COLOR(page) ((page_to_pfn(page)&(BANK_COLOR2))>>6 | (page_to_pfn(page)&(BANK_COLOR1))>>1)  
 */
/*
 * yanghao
 * date:20150515
 * target:compare the difference of two literation
 */
unsigned int hotpage_history[BANK_NUM][10000], hotpage_new[BANK_NUM][10000];
int history_index[BANK_NUM], new_index[BANK_NUM];
int differences[BANK_NUM];
int frequence[BANK_NUM][10000];

void add_array(unsigned long tag, int bank)
{
    int i;
    for(i=0;i<new_index[bank];i++)
    {
        if(hotpage_new[bank][i]==tag)
        {
            frequence[bank][i]++;
            return;
        }
    }
    hotpage_new[bank][new_index[bank]]=tag;
    new_index[bank]++;
}
void compare_array()
{
    int i,j,k;
    for(i=0;i<BANK_NUM;i++)
    {
        for(j=0;j<history_index[i];j++)
        {
            for(k=0;k<new_index[i];k++)
            {
                if(hotpage_history[i][j]==hotpage_new[i][k])
                    break;
            }
            if(hotpage_history[i][j]!=hotpage_new[i][k])
                differences[i]++;
        }
    }
    return;
}
void copy_to_history()
{
    int i,j;
    for(i=0;i<BANK_NUM;i++)
    {
        for(j=0;j<new_index[i];j++)
        {
            hotpage_history[i][j]=hotpage_new[i][j];
        }
        history_index[i]=new_index[i];
        new_index[i]=0;
    }
    return;
}

//yanghao2014@20150517:record the total number of hotpages.
//yanghao2014@20150520:record the frequence of hotpages.
unsigned long total_hotpage[BANK_NUM][30000];
//unsigned long frequence_hotpage[BANK_NUM][30000];
int total_index[BANK_NUM];
void add_total_array(unsigned long tag, int bank)
{
    int i;
    for(i=0;i<total_index[bank];i++)
    {
        if(total_hotpage[bank][i]==tag)
        {
            //frequence_hotpage[bank][i]++;
            return;
        }
    }
    total_hotpage[bank][total_index[bank]]=tag;
    total_index[bank]++;
}

//begin to cal. the number of hot pages. And we will re-do it in every 5 seconds.
static void time_handler(unsigned long data)
{
     int win=0;
     mod_timer(&stimer, jiffies + 5*HZ);
     win = scan_pgtable(); // 1 is win.
     if(!win) // we get no page, maybe something wrong occurs
          printk("leiliu: fail in scanning page table. goooooooo......\n");
}

static int __init timer_init(void)
{

     random_page = 50;
     loops = 0;//yanghao:init the NO. of random_page.

     //yanghao
     int i,j;
     for(i=0;i<BANK_NUM;i++)
     {
          history_index[i]=0;
          new_index[i]=0;
          total_index[i]=0;
     }

     printk("leiliu: module init!\n");
     init_timer(&stimer);
     stimer.data = 0;
     stimer.expires = jiffies + 5*HZ;  
     stimer.function = time_handler;
     add_timer(&stimer);
     return 0;
}

static void __exit timer_exit(void)
{
     //yanghao:
     int i,j;
     if (loops != 0)
     {
         for (j=0; j<300000; j++)
         {
             page_read_times[j] = 0;
             page_write_times[j] = 0;
         }
         highr_yanghao=0, highw_yanghao=0, midhigh_yanghao=0, mid_yanghao=0, midlow_yanghao=0;
         for (i=0; i<loops; i++)
         {
             for (j=0; j<300000; j++)
             {
                 if (history[i][j] == 1)
                     page_read_times[j]++;
                 if (history[i][j] == 2)
                     page_write_times[j]++;
             }
         }
         for (j=0; j<300000; j++)
         {
             if (page_read_times[j]==0 && page_write_times[j]!=0)
             {
                 highw_yanghao++;
                 continue;
             }
             if (page_read_times[j]!=0 && page_write_times[j]==0)
             {
                 highr_yanghao++;
                 continue;
             }
             if ((double)page_read_times[j]/page_write_times[j] > 2)
             {
                 midhigh_yanghao++;
                 continue;
             }
             if ((double)page_read_times[j]/page_write_times[j] < 2
                && (double)page_read_times[j]/page_write_times[j] > 0.5)
             {
                 mid_yanghao++;
                 continue;
             }
             if ((double)page_read_times[j]/page_write_times[j] < 0.5)
                 midlow_yanghao++;
         }
         printk("[LOG]after sampling ...\n");
         printk("the values denote RD/WD.\n");
         printk("-->only RD is %d.\n--> only WD is %d.\n-->RD/WD locates in (2,--) is %d. Indicate RD >> WD.\n-->RD/WD locates in [0.5,2] is %d. Indicate RD :=: WD.\n-->RD/WD locates in (0,0.5) is %d. Indicate RD << WD.\n", highr_yanghao, highw_yanghao, midhigh_yanghao, mid_yanghao, midlow_yanghao);
     }//end yanghao

     //yanghao2014@20150517
     printk("The total HOTpage is:");
     for(j=0;j<BANK_NUM;j++)
     {
         printk("bank %d: %d ",j,total_index[j]);
     }
     printk("\n");

     printk("Unloading leiliu module.\n");
     del_timer(&stimer);//delete the timer
     return;
}

#if 1
//get the process of current running benchmark. The returned value is the pointer to the process.
static struct task_struct * traver_all_process(void) 
{
	struct pid * pid;
  	pid = find_vpid(process_id);
  	return pid_task(pid,PIDTYPE_PID);
}
#endif

#if 1
//pgtable sequential scan and count for __access_bits
static int scan_pgtable(void)
{
     pgd_t *pgd = NULL;
     pud_t *pud = NULL;
     pmd_t *pmd = NULL;
     pte_t *ptep, pte;
     spinlock_t *ptl;

     //unsigned long tmp=0; // used in waiting routine
     struct mm_struct *mm;
     struct vm_area_struct *vma;
     unsigned long start=0, end=0, address=0;
     int number_hotpages = 0, number_vpages=0;
     int tmpp;
     int hot_page[200];
     struct task_struct *bench_process = traver_all_process(); //get the handle of current running benchmark
     int i, j, times;
 
     if(bench_process == NULL)
     {
          printk("leiliu: get no process handle in scan_pgtable function...exit&trying again...\n");
          return 0;
     }
     else // get the process
          mm = bench_process->mm;
     if(mm == NULL)
     {
          printk("leiliu: error mm is NULL, return back & trying...\n");          
          return 0;
     }

     j=0;
     for(;j<300000;j++)
     {
        shadow1[j]=-1;
        bank_index_shadow[j] = -1;
        //yanghao
        read_times[j] = 0;
        write_times[j] = 0;
        history[loops][j] = 0;
        //end
     }
     for(j=0;j<=199;j++)
     {
        hot_page[j]=0;
        //yanghao
        dirty_page[j] = 0;
        reuse_time[j] = 0;
     }
     
     for(j=0;j<BANK_NUM;j++)
     {
         bank_map[j] = 0;
         bank_map_hotpage[j] = 0;
         bank_acc_count[j] = 0;
         differences[j] = 0;
         for(i=0;i<10000;i++)
             frequence[j][i]=0;
     }

     //yanghao
     times = 0;
      
     //printk("re-set shadow\n");
     for(tmpp=0;tmpp<200;tmpp++)
     {
       number_hotpages = 0;
       //scan each vma
       for(vma = mm->mmap; vma; vma = vma->vm_next)
       {
          page_counts = 0;
          start = vma->vm_start;
          end = vma->vm_end;
          mm = vma->vm_mm;
          //in each vma, we check all pages 
          for(address = start; address < end; address += PAGE_SIZE)
          {
              //scan page table for each page in this VMA
              pgd = pgd_offset(mm, address);
              if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
                  continue;
              pud = pud_offset(pgd, address);
              if (pud_none(*pud) || unlikely(pud_bad(*pud)))
                  continue;
              pmd = pmd_offset(pud, address);
              if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
                  continue;
              ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
              pte = *ptep;
              if(pte_present(pte))
              {   
                  if(pte_young(pte)) // hot page
                  {
                      //re-set and clear  _access_bits to 0
                      pte = pte_mkold(pte); 
                      set_pte_at(mm, address, ptep, pte);
                      //yanghao:re-set and clear _dirty_bits to 0
                      pte = pte_mkclean(pte);
                      set_pte_at(mm, address, ptep, pte);
                  }
              }
              pte_unmap_unlock(ptep, ptl);
              page_counts++;
          } // end for(adddress .....)
       } // end for(vma ....)
        //5k instructions in idle 
       // for(tmp=0;tmp<200*5;tmp++) {;} //1k instructions = 200 loops. 5 instructions/per loop. 
 
        //count the number of hot pages
        if(bench_process == NULL)
        {   
           printk("leiliu1: get no process handle in scan_pgtable function...exit&trying again...\n");
           return 0;
        }   
        else // get the process
           mm = bench_process->mm;
        if(mm == NULL)
        {   
           printk("leiliu1: error mm is NULL, return back & trying...\n");    
           return 0;
        } 
        number_vpages = 0;

        sampling_interval = page_counts/250;//yanghao:
        page_counts = 0;

        for(vma = mm->mmap; vma; vma = vma->vm_next)
        {
           start = vma->vm_start;
           end = vma->vm_end;
           //scan each page in this VMA
           mm = vma->vm_mm;
           int pos=0;
           for(address = start; address < end; address += PAGE_SIZE)
           {
               //scan page table for each page in this VMA
               pgd = pgd_offset(mm, address);
               if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
                     continue;
               pud = pud_offset(pgd, address);
               if (pud_none(*pud) || unlikely(pud_bad(*pud)))
                     continue;
               pmd = pmd_offset(pud, address);
               if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
                     continue;
               ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
               pte = *ptep;
               if(pte_present(pte))
               {
                     /**
                       * liulei@20150326
                       * record the distribution of *ALL* the physical pages of specific app in DRAM banks
                       */
                     struct page * tmp_page = pte_page(pte);
                     int bank=PAGE_TO_COLOR(tmp_page);
                     if(tmpp==0)
                          bank_map[bank]++;

                     if(pte_young(pte)) // hot pages
                     {
                           int now = pos + number_vpages; 
                           //shadow[now]++;
                           shadow1[now]++;
                           hot_page[tmpp]++;

                         /**
                           * liulei@20150326
                           * record the bank index to which each physical *HOT* page will visit
                           */
                         //if(tmpp==0) // we just record the first-time information in each loop
                         bank_map_hotpage[bank]++;
                         add_array((unsigned long)page_to_pfn(tmp_page),bank);
                         add_total_array((unsigned long)page_to_pfn(tmp_page),bank);

                         //record the access logic times of each page
                         bank_index_shadow[page_counts]=bank;

                           //yanghao:
                           if (page_counts == random_page)
                           {
                               times++;
                           }
                           if (pte_dirty(pte))
                           {
                               write_times[now]++;
                               if (page_counts == random_page)
                               {
                                   dirty_page[tmpp] = 1;
                               }
                           }
                           else
                               read_times[now]++;
                           
                     }
                     else
                     {
                           if (page_counts == random_page)
                               reuse_time[times]++;
                     }//end
               }
               pos++;
               pte_unmap_unlock(ptep, ptl);
               page_counts++;
           } //end for(address ......)
           number_vpages += (int)(end - start)/PAGE_SIZE;
         } // end for(vma .....) */ 
      } //end 200 times repeats
      //yanghao:cal. the No. of random_page
      random_page += sampling_interval;
      if(random_page >= page_counts)
           random_page=page_counts/300;
//////////////////////////////////////////////
      //output after 200 times
/*      foo=0;
      for(j=0;j<30*10000;j++)
      {
          if(shadow1[j]>-1)
              foo++;
      }
*/
      int avg_page_utilization, avg_hotpage, num_access;
      int hig, mid, low, llow, lllow, llllow, all_pages;
      int ri, wi;
  
      hig=0,mid=0,low=0,llow=0,lllow=0,llllow=0,all_pages=0;
/*      for(j=0;j<30*100*100;j++)
      {
         if(shadow1[j]<200 && shadow1[j]>150)
              hig++;
         if(shadow1[j]>100 && shadow1[j]<=150)
              mid++;
         if(shadow1[j]<=100 && shadow1[j]>64)
              low++;
         if(shadow1[j]>10 && shadow1[j]<=64)
              llow++;
         if(shadow1[j]>5 && shadow1[j]<=10)
              lllow++;
         if(shadow1[j]>=0 && shadow1[j]<=5)
              llllow++;
         if(shadow1[j]>-1)
              all_pages++;
      }

      //the values reflect the accessing frequency of each physical page.
      printk("[LOG: after sampling (200 loops) ...] ");
      printk("the values denote the physical page accessing frequence.\n"); 
      printk("-->hig (150,200) is %d. Indicating the number of re-used pages is high.\n-->mid (100,150] is %d.\n-->low (64,100] is %d.\n-->llow (10,64] is %d. In locality,no too many re-used pages.\n-->lllow (5,10] is %d.\n-->llllow [1,5] is %d.\n", hig, mid, low, llow, lllow, llllow);


      avg_hotpage=0; //the average number of hot pages in each iteration.
      for(j=0;j<200;j++)
         avg_hotpage+=hot_page[j];
      avg_hotpage/=(j+1); 

      /*  
       * new step@20140704
       * (1)the different phases of memory utilization
       * (2)the avg. page accessing utilization
       * (3)memory pages layout and spectrum
       */
/*      num_access=0; //the total number of memory accesses across all pages
      for(j=0;j<30*100*100;j++)
          if(shadow1[j]>-1) //the page that is accessed at least once
              num_access+=(shadow1[j]+1);

      printk("the total number of memory accesses is %d, the average is %d\n",num_access, num_access/200);
      avg_page_utilization=num_access/all_pages;
      printk("Avg hot pages num is %d, all used pages num is %d, avg utilization of each page is %d\n", avg_hotpage, all_pages, avg_page_utilization);
      //yanghao:print the information about reuse-distance
      if ((times == 0) && (reuse_time[0] ==0))
          printk("the page No.%d is not available.",random_page);
      else
      {
          if ((times == 0) && (reuse_time[0] == 0))
              printk("the page No.%d was not used in this 200 loops.",random_page);
          else
          {
              if (times < 200)
                  times++;
              printk("the reusetime of page No.%d is:",random_page);
              for (j = 0; j < times; j++)
                  printk("%d ",reuse_time[j]);
              printk("\n");
              printk("the page No.%d is dirty at:",random_page);
              for (j = 0; j < 200; j++)
                  if (dirty_page[j] == 1)
                      printk("%d ",j);
          }
      }
      printk("\n");

      //yanghao:print the information about reading & writting.
      w2r = 0, r2w = 0, ri = 0, wi = 0;
      for (j=0; j<300000; j++)
      {
          if (read_times[j] > write_times[j] * 2)
          {
              if (out_data[j] == 2)
                  w2r++;
              out_data[j] = 1;
              history[loops][j] = 1;
              ri++;
              continue;
          }
          else
          {
              if (write_times[j] > 0)
              {
                  if (out_data[j] == 1)
                      r2w++;
                  history[loops][j] = 2;
                  out_data[j] = 2;
                  wi++;
              }
          }
      }
      loops++;
      printk("The number of reading dominant pages is: %d .\n",ri);
/*      gap = (i<200?1:(i/200));
      for (j=0;j<i;j+=gap)
          printk("%d ",out_data[j]);
      
      for (i=0, j=0; j<300000; j++)
          if (read_times[j] < write_times[j])
              out_data[i++] = j;
*/
/*      printk("The number of writing dominant pages is: %d .\n",wi);
/*
      gap = (i<200?1:(i/200));
      for (j=0;j<i;j+=gap)
          printk("%d ",out_data[j]); 
*/
/*      printk("The number of pages(RD --> WD) is: %d \nThe number of pages(WD --> RD) is: %d \n",r2w,w2r);
      printk("\n");

       compare_array();

       printk("the number of hotpage changes is:\n");
       for(j=0;j<BANK_NUM;j++)
           printk("bank%d %ld, ",j,differences[j]);
       printk("\n");

      /////////////////////////////      

      /**
       * liulei@20150326
       * output the distribution of all pages at DRAM bank level, and the unbalance-balance details of hot pages
       */
/*      unsigned long max_u,min_u;
      max_u=0,min_u=LONG_MAX;//yanghao:initialize min_u with LONG_MAX
      printk("bank distribution for ALL pages:");
      for(j=0;j<32;j++)
      {
          if(bank_map[j]>max_u) max_u=bank_map[j];
          if(bank_map[j]<min_u) min_u=bank_map[j];
          //printk("max is %ld, min is %ld, diff is %ld\n", max_u,min_u,(max_u-min_u));
          printk("bank%d %ld, ",j,bank_map[j]);
      }
      printk("\n");
      printk("max is %ld, min is %ld, diff is %ld\n", max_u,min_u,(max_u-min_u));

      //show the distribution of *HOT pages*, and the hot graduate of DRAM banks, and if the graduate is hard to be described, we show the diff between max and min 
      max_u=0,min_u=LONG_MAX;//yanghao:initialize min_u with LONG_MAX
      printk("bank distribution for HOT pages:");
      for(j=0;j<32;j++)
      {
          if(bank_map[j]>max_u) max_u=bank_map[j];
          if(bank_map[j]<min_u) min_u=bank_map[j];
          //printk("max is %ld, min is %ld, diff is %ld\n", max_u,min_u,(max_u-min_u));
          printk("bank%d %ld, ",j,bank_map_hotpage[j]);
      }
      printk("\n");
      printk("max is %ld, min is %ld, diff is %ld\n", max_u,min_u,(max_u-min_u));

      //show the logic access times in each DRAM bank
      for(j=0;j<30*100*100;j++)
          //calculate the number of access times in each DRAM bank 
          if(shadow1[j]>-1)
               bank_acc_count[bank_index_shadow[j]]+=shadow1[j];

      max_u=0,min_u=LONG_MAX;//yanghao:initialize min_u with LONG_MAX
      printk("bank access times:");
      for(j=0;j<32;j++)
      {
          if(bank_map[j]>max_u) max_u=bank_map[j];
          if(bank_map[j]<min_u) min_u=bank_map[j];
          //printk("max is %ld, min is %ld, diff is %ld\n", max_u,min_u,(max_u-min_u));
          printk("bank%d %ld, ",j,bank_acc_count[j]);
      }
      printk("\n");
      printk("max is %ld, min is %ld, diff is %ld\n", max_u,min_u,(max_u-min_u));
*/
       //yanghao2014@20150521:print the frequence of hotpage
       int threshold,hhig,hhhig,hhhhig,hmid,lmid;
       printk("The number of very hot page is:");
       for(i=0;i<BANK_NUM;i++)
       {
            //threshold=0;
            hhhhig=0,hhhig=0,hhig=0,hig=0,hmid=0,mid=0,lmid=0,low=0,llow=0,lllow=0,llllow=0;
            for(j=0;j<new_index[i];j++)
            {
                if(frequence[i][j]>150)
                {
                    hhhhig++;
                    continue;
                }
                if(frequence[i][j]>125)
                {
                    hhhig++;
                    continue;
                }
                if(frequence[i][j]>100)
                {
                    hhig++;
                    continue;
                }
                if(frequence[i][j]>80)
                {
                    hig++;
                    continue;
                }
                if(frequence[i][j]>60)
                {
                    hmid++;
                    continue;
                }
                if(frequence[i][j]>45)
                {
                    mid++;
                    continue;
                }
                if(frequence[i][j]>30)
                {
                    lmid++;
                    continue;
                }
                if(frequence[i][j]>20)
                {
                    low++;
                    continue;
                }
                if(frequence[i][j]>10)
                {
                    llow++;
                    continue;
                }
                if(frequence[i][j]>5)
                {
                    lllow++;
                    continue;
                }
                else
                {
                    llllow++;
                    continue;
                }
           }
           printk("bank %d:\n",i);
           printk("(150,200]:%d\t(125,150]:%d\t(100,125]:%d\t(80,100]:%d\t(60,80]:%d\t(45,60]:%d\t(30,45]:%d\t(20,30]:%d\t(10,20]:%d\t(5,10]:%d\t(0,5]:%d\t\n",hhhhig,hhhig,hhig,hig,hmid,mid,lmid,low,llow,lllow,llllow);
       }
       printk("\n");
       copy_to_history();

      printk("\n\n");
      return 1;
}
#endif

module_init(timer_init);
module_exit(timer_exit);
MODULE_AUTHOR("leiliu");
MODULE_LICENSE("GPL");
