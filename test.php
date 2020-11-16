<?php
$fn  = "test{$argv[1]}.txt";
$ctx = str_repeat("Hello World!\n", 1000);
echo hex2bin(sprintf("%02x", strlen($fn)));
echo $fn."\0".str_repeat("\0", 255 - strlen($fn) - 1);
echo hex2bin(sprintf("%016x", strlen($ctx) * 3));
echo $ctx;
sleep(rand(0, 10));
echo $ctx;
sleep(rand(0, 10));
echo $ctx;
