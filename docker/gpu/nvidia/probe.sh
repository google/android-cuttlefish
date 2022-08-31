#!/bin/bash -r

test $(lspci | grep -iE '(vga|3D controller)' | grep -icw nvidia) -gt 0
