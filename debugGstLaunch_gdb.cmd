#file gst-launch
#set args ./debugGstLaunch.cfg
handle SIGQUIT pass nostop noprint
handle SIGUSR1 pass nostop noprint
handle SIGUSR2 pass nostop noprint
handle SIGTRAP nostop
handle SIGINT stop
run
init-if-undefined $_exitcode = 9999
if $_exitcode != 9999
   quit
else
   where
end
