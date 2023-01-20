package com.android.minidroid.testservice;

interface ITestService {
    const int SERVICE_PORT = 5678;

    /* make server process print 'Hello World' to stdout. */
    void sayHello();

    /* make server process print @{text} to stdout. */
    void printText(String text);

    /* add two integers and return the result. */
    int addInteger(int a, int b);

}
