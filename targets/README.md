Platforms
---------

th12: branched from Econotag platform in mainline contiki 
    - defined LEDs and DHT sensor pin

th12-lowpower: this is a hack that works around having different
    configs built in Contiki.

The problem is that the lowpower th12 needs to be configured as a leaf
node which is a preprocessor define. This requires the object file to
be rebuilt and relinked into the .bin. In the Contiki build system,
the only way this is possible is to have a special platform for
it. (This would be anlogus to running ./configure with a different set
of options)
