#
# Regular cron jobs for the smartvirdebug package
#
0 4	* * *	root	[ -x /usr/bin/smartvirdebug_maintenance ] && /usr/bin/smartvirdebug_maintenance
