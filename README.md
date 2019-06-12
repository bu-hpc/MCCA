# MCCA
A Framework for Rapid Exploration of PDES Communication Subsystem on Manycore Clusters

usage: mcca threads ppn percent_rthread percent_rprocess percent_remote epg -debug (optional)

    threads           number of execution threads
    ppn               number of processes per node
    percent_rthread   percentage of messages whose destination is a thread (PE) in this process
    percent_rprocess  percentage of messages whose destination is a thread (PE) in another process
    percent_remote    percentage of messages whose destination is a thread (PE) in another machine
    epg               loop count of floating point calculations to provide a synthetic workload
    -debug            enable debug messages (may effect performance)
