Demonstration Firmware for the TH12
===================================

coap-post
---------

This program is a simple "always on" RPL node with full routing. No
power managment is done of any kind so therefore the batteries will
only last a few days.

The node will recieve an IPv6 prefix from the RPL network that it
joins. Once this happens, it will post its sensor data as JSON to the
following:

coap://[prefix::1]


coap-post-sleep
---------------

This is a modified version of the simpler `coap-post` program. When
first powered up, the node stays on for ON_POWER_WAKE_TIME (default
300 seconds). This gives you a chance to probe the node (such as query
its rplinfo resources) or whatever.

After ON_POWER_WAKE_TIME has expired, two things can happen. If the
node has joined a DAG, it will go to sleep until it is time to take a
sample and post its data. If it hasn't joined a DAG, then the node
will sleep for NO_DAG_SLEEP_TIME (default 300 seconds), and then stay
on for LOOKING_FOR_DAG_SLEEP_TIME (default 10 seconds) while it
retries to find a RPL network. If it has joined a DAG, then the node
sleeps for POST_INTERVAL, then posts the data and repeates.
