Knumcap
===================

The simple application that displays notify messages in Linux when caps lock or num lock keys are pressed.

----------

Story
-------------

I have Lenovo laptop that doesn't have leds that could notify me about caps lock or num lock press event. In Windows OS Lenovo gives hotkey utility that can notify user about caps lock or num lock press event on Linux such utility doesn't exist.

### How to build

I have used Eclipse CDT IDE. In repository I put Eclipse project so import it to this IDE and press build button in IDE. You are ready to hack now. Before importing project you must to install libx11-dev on your Linux box:

```bash
$ sudo apt-get install libx11-dev 
```

After requirement dependency are installed just build project and run program:

```bash
$ chmod u+x knumcap
$ ./knumcap
```

Now you will be notified when caps lock or num lock keys are pressed :)
