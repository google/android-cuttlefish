#!/bin/bash -r

test $(lspci | grep -i vga | grep -icw nvidia) -gt 0
