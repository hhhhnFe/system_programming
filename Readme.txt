The goal of this project was to design and implement my own shell program.
It is devided into three phases, each complemented than before

Phase 1)
   - Supports compiled binary linux commands
   - Supports cd by defining builtin command
   - Shell do not terminate itself by Ctrl+C
   - Shell only offers foreground jobs
   - Fully handles quotation marks like bash shell (execpt nested form)
   - Run-ex) ls -al

Phase 2)
   - Extended from Phase 1
   - Supports pipelining
   - pipelines do not have to be separted by spaces
   - Run-ex) ls -al | grep c|sort -r

Phase 3)
   - Shell now offers background jobs
   - Offers job control built-in commands
       1) jobs : List the running and stopped background jobs
       2) bg %x : Change a stopped background job to running background job
       3) fg %x : Change a stopped or running background job to a running foreground job
       4) kill %x : Terminate a job
             
            ( Notification : x must be a index ) 
   - Foreground now gains control over terminal and can be stopped by Ctrl+Z
     which does not effect other jobs
   - Same with Ctrl+C
   - Run-ex) sleep 10&

Basic flow and its interpretation is based on 
    1) Computer Systems : A Programmer's Perspective, by Randal E. Bryant and David R. O'Hallaron
    2) The GNU C Library Reference Manual
