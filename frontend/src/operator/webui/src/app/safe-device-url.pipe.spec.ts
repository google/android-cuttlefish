import {SafeDeviceUrlPipe} from './safe-device-url.pipe';
import {BrowserModule, DomSanitizer} from '@angular/platform-browser';
import {TestBed} from '@angular/core/testing';

describe('SafeDeviceUrlPipe', () => {
  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [BrowserModule],
    });
  });

  it('create an instance', () => {
    const domSanitizer = TestBed.inject(DomSanitizer);
    const pipe = new SafeDeviceUrlPipe(domSanitizer);
    expect(pipe).toBeTruthy();
  });
});
