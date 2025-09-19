// This file is required by karma.conf.js and loads recursively all the .spec and framework files

import 'zone.js/testing';
import {provideZoneChangeDetection, NgModule} from '@angular/core';
import {getTestBed} from '@angular/core/testing';
import {
  BrowserDynamicTestingModule,
  platformBrowserDynamicTesting,
} from '@angular/platform-browser-dynamic/testing';

@NgModule({ providers: [ provideZoneChangeDetection() ] })
export class ZoneChangeDetectionModule {}

// First, initialize the Angular testing environment.
getTestBed().initTestEnvironment(
  [ZoneChangeDetectionModule, BrowserDynamicTestingModule],
  platformBrowserDynamicTesting()
);
