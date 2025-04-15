#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "cache.h"
using namespace std;

Cache::Cache(int s,int a,int b )
{
   
   ulong i, j;

 lineSize = (ulong)(b);
        sets = 1;               // Only one set for fully associative
        assoc = 10000;        // Large associativity to simulate "infinite" cache
        numLines = assoc;       // The number of lines is equal to associativity
  
   
 
cache = new cacheLine*[sets];
    for (i = 0; i < sets; i++) {
        cache[i] = new cacheLine[assoc];
        for (j = 0; j < assoc; j++) {
            cache[i][j].invalidate();
        }
    }    
   
}

// MESI Processor Access Function
void Cache::MESI_Processor_Access(ulong addr, uchar rw, int copy, Cache **cache, int processor, int num_processors) {
    cacheLine *line = findLine(addr);

    if (rw == 'r') { // Processor Read
        reads++;
        if (!line || !line->isValid()) { // State: Invalid
            readMisses++;
            mem_trans++;

            // Total execution time is increased for memory access (no writeback for reads)
            Total_execution_time += memory_latency;

            bool copiesExist = (copy == 1);
            line = fillLine(addr);
            if (copiesExist) {
                line->setFlags(Shared);  // State transition: Invalid -> Shared
            } else {
                line->setFlags(Exclusive);  // State transition: Invalid -> Exclusive
            }

            // Notify other caches about BusRd
            for (int i = 0; i < num_processors; i++) {
                if (i != processor) {
                    cache[i]->MESI_Bus_Snoop(addr, 1, 0, 0); // Bus Read
                }
            }

        } else { // Cache hit
            Readhits++;
            Total_execution_time += read_hit_latency; // Read hit latency
        }

    } else if (rw == 'w') { // Processor Write
        writes++;
        if (!line || !line->isValid()) { // State: Invalid
            writeMisses++;
            mem_trans++;
            busrdxes++;

            // Total execution time includes memory access latency for write miss
            //Total_execution_time += memory_latency;

            line = fillLine(addr);
            line->setFlags(Modified);  // State transition: Invalid -> Modified

            // Notify other caches about BusRdX
            for (int i = 0; i < num_processors; i++) {
                if (i != processor) {
                    cache[i]->MESI_Bus_Snoop(addr, 0, 1, 0); // Bus Read Exclusive
                }
            }

        } else if (line->getFlags() == Shared) { // State: Shared -> Modified
            Writehits++;
            busupgr++;

            // Total execution time is increased for write hit latency
            Total_execution_time += write_hit_latency;

            line->setFlags(Modified);  // State transition: Shared -> Modified

            // Notify other caches about BusUpgrade
            for (int i = 0; i < num_processors; i++) {
                if (i != processor) {
                    cache[i]->MESI_Bus_Snoop(addr, 0, 0, 1); // Bus Upgrade
                }
            }

        } else if (line->getFlags() == Exclusive || line->getFlags() == Modified) { // State remains Modified
            Writehits++;
            Total_execution_time += write_hit_latency; // Write hit latency
            line->setFlags(Modified);
        }
    }
}

void Cache::MESI_Bus_Snoop(ulong addr, int busread, int busreadx, int busupgrade) {
    cacheLine *line = findLine(addr);
    if (!line || !line->isValid()) return;

    // Case 1: BusRd and BusRdX both set
    if (busread && busreadx) { 
        if (line->getFlags() == Modified || line->getFlags() == Exclusive || line->getFlags() == Shared) {
            flushes++;                // Increment flushes for both BusRd and BusRdX
            Total_execution_time += flush_transfer; // Account for flush latency
            
            if (line->getFlags() == Modified) {
                mem_trans++;          // Increment memory transaction for Modified state
            }

            line->invalidate();       // Transition to Invalid
        }
        return; // Return here to avoid double handling of individual BusRd/BusRdX
    }

    // Case 2: Only BusRd
    if (busread) {
        if (line->getFlags() == Modified) {
            flushes++;
            mem_trans++;
            Total_execution_time += flush_transfer;
            line->setFlags(Shared); // Transition M -> S
        } else if (line->getFlags() == Exclusive) {
            flushes++;
            Total_execution_time += flush_transfer;
            line->setFlags(Shared); // Transition E -> S
        } else if (line->getFlags() == Shared) {
            flushes++;
            Total_execution_time += flush_transfer; // FlushOpt for S state
        }
    }

    // Case 3: Only BusRdX
    if (busreadx) {
        if (line->getFlags() == Modified) {
            flushes++;
            mem_trans++;
            Total_execution_time += flush_transfer;
        }
        invalidations++;
        line->invalidate(); // Transition to Invalid
    }

    // Case 4: BusUpgrade
    if (busupgrade) {
        if (line->getFlags() == Shared) {
            invalidations++;
            line->invalidate(); // Transition S -> I
        }
    }
}


// MOESI Processor Access Function
void Cache::MOESI_Processor_Access(ulong addr, uchar rw, int copy, Cache **cache, int processor, int num_processors) {
    cacheLine *line = findLine(addr);

    if (rw == 'r') { // Processor Read
        reads++;
        if (!line || !line->isValid()) { // State: Invalid
            readMisses++;
            Total_execution_time += memory_latency; // Memory access latency
            mem_trans++;

            line = fillLine(addr);
            if (copy == 1) {
                line->setFlags(Shared); // Transition: Invalid -> Shared
            } else {
                line->setFlags(Exclusive); // Transition: Invalid -> Exclusive
            }

            // Notify other caches about BusRd
            for (int i = 0; i < num_processors; i++) {
                if (i != processor) {
                    cache[i]->MOESI_Bus_Snoop(addr, 1, 0, 0); // Bus Read
                }
            }

        } else if (line->getFlags() == Shared || line->getFlags() == Owner ||
                   line->getFlags() == Exclusive || line->getFlags() == Modified) {
            Readhits++;
            Total_execution_time += read_hit_latency; // Read hit latency
        }

    } else if (rw == 'w') { // Processor Write
        writes++;
        if (!line || !line->isValid()) { // State: Invalid
            writeMisses++;
            busrdxes++;
            Total_execution_time += memory_latency; // Memory access latency
            mem_trans++;

            line = fillLine(addr);
            line->setFlags(Modified); // Transition: Invalid -> Modified

            // Notify other caches about BusRdX
            for (int i = 0; i < num_processors; i++) {
                if (i != processor) {
                    cache[i]->MOESI_Bus_Snoop(addr, 0, 1, 0); // Bus Read Exclusive
                }
            }

        } else if (line->getFlags() == Shared || line->getFlags() == Owner) {
            Writehits++;
            busupgr++;
            Total_execution_time += write_hit_latency; // Write hit latency

            line->setFlags(Modified); // Transition: Shared/Owner -> Modified

            // Notify other caches about BusUpgrade
            for (int i = 0; i < num_processors; i++) {
                if (i != processor) {
                    cache[i]->MOESI_Bus_Snoop(addr, 0, 0, 1); // Bus Upgrade
                }
            }

        } else if (line->getFlags() == Exclusive || line->getFlags() == Modified) {
            Writehits++;
            Total_execution_time += write_hit_latency; // Write hit latency
            line->setFlags(Modified); // State remains Modified
        }
    }
}

// MOESI Bus Snoop Function
void Cache::MOESI_Bus_Snoop(ulong addr, int busread, int busreadx, int busupgrade) {
    cacheLine *line = findLine(addr);
    if (!line || !line->isValid()) return;

    if (busread) { // Bus Read
        if (line->getFlags() == Modified || line->getFlags() == Owner) { // Transition: Modified -> Owner
            flushes++; // Increment flush count
            //mem_trans++; // Memory transaction for flush
            Total_execution_time += flush_transfer; // Flush transfer latency
            line->setFlags(Owner); // State transition: Modified -> Owner
           
        } else if (line->getFlags() == Exclusive) { // Transition: Exclusive -> Shared
            line->setFlags(Shared); // State transition: Exclusive -> Shared
        }

    } else if (busreadx) { // Bus Read Exclusive
        if (line->getFlags() == Modified || line->getFlags() == Owner) { // Flush required
            flushes++; // Increment flush count
            Total_execution_time += flush_transfer; // Flush transfer latency
            mem_trans++; // Memory transaction for flush
        }
        if (line->getFlags() != INVALID) { // Invalidate line
            invalidations++;
            line->invalidate(); // State transition: Any -> Invalid
        }

    } else if (busupgrade) { // Bus Upgrade
        if (line->getFlags() == Shared) {
            invalidations++; // Increment invalidation count
            line->invalidate(); // State transition: Shared -> Invalid
        }
        else if (line ->getFlags()==Owner) {
            invalidations++;
            line->invalidate();
        }
    }
}

 //look up line
cacheLine * Cache::findLine(ulong addr)
{
   ulong i, j, tag, pos;
   
   pos = assoc;
   tag = calcTag(addr);
   i   = calcIndex(addr);
  
   for(j=0; j<assoc; j++)
	if(cache[i][j].isValid())
	        if(cache[i][j].getTag() == tag)
		{
		     pos = j; break; 
		}
   if(pos == assoc)
	return NULL;
   else
	return &(cache[i][pos]); 
}


//Most of these functions are redundant so you can use/change it if you want to

//upgrade LRU line to be MRU line
void Cache::updateLRU(cacheLine *line)
{
  line->setSeq(currentCycle);  
}

//return an invalid line as LRU, if any, otherwise return LRU line
cacheLine * Cache::getLRU(ulong addr)
{
   ulong i, j, victim, min;

   victim = assoc;
   min    = currentCycle;
   i      = calcIndex(addr);
   
   for(j=0;j<assoc;j++)
   {
      if(cache[i][j].isValid() == 0) 
	  return &(cache[i][j]);     
   }   
   for(j=0;j<assoc;j++)
   {
	 if(cache[i][j].getSeq() <= min) { victim = j; min = cache[i][j].getSeq();}
   } 
   assert(victim != assoc);
  // std::cout << "victim" << victim << std::endl;
   return &(cache[i][victim]);
}

//find a victim, move it to MRU position
cacheLine *Cache::findLineToReplace(ulong addr)
{
   cacheLine * victim = getLRU(addr);
   updateLRU(victim);
  
   return (victim);
}

//allocate a new line
cacheLine *Cache::fillLine(ulong addr)
{ 
   ulong tag;
  
   cacheLine *victim = findLineToReplace(addr);

   assert(victim != 0);
   if ((victim->getFlags() == Modified))
   {
	   writeBack(addr);
   }
   victim->setFlags(Shared);	
   tag = calcTag(addr);   
   victim->setTag(tag);
       
 

   return victim;
}

void Cache::printStats()
{ 
	//printf("===== Simulation results      =====\n");
	float miss_rate = (float)(getRM() + getWM()) * 100 / (getReads() + getWrites());
	
printf("01. number of reads:                                 %10lu\n", getReads());
printf("02. number of read misses:                           %10lu\n", getRM());
printf("03. number of writes:                                %10lu\n", getWrites());
printf("04. number of write misses:                          %10lu\n", getWM());
printf("05. number of write hits:                            %10lu\n", getWH());
printf("06. number of read hits:                             %10lu\n", getRH()); // Changed from getRM() to getRH()
printf("07. total miss rate:                                 %10.2f%%\n", miss_rate);
printf("08. number of memory accesses (exclude writebacks):  %10lu\n", mem_trans);
printf("09. number of invalidations:                         %10lu\n", Invalidations());
printf("10. number of flushes:                               %10lu\n", flushes);

	
}