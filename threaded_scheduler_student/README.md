# project2-csc453

Hannah Moshtaghi
Arianna Wilde 

Question answer: Busy waiting is when a a thread keeps checking over and over in a loop whether something is ready instead of just going to sleep and waiting to be woken up. An example of this would be a worker thread that sits in a while loop like: 

while (no_work_availible){
    // spin forever 
}

repeatedly checking that condition without ever sleeping. 


This was prohibited in the assignment because it wastes valuable CPU time for no reason when the thread is supposed to be idle. 
