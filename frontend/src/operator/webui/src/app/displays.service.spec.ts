import {TestBed} from '@angular/core/testing';
import {provideHttpClient} from '@angular/common/http';
import {provideHttpClientTesting} from '@angular/common/http/testing';

import {DisplaysService} from './displays.service';

describe('DisplaysService', () => {
  let service: DisplaysService;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [provideHttpClient(), provideHttpClientTesting()],
    });
    service = TestBed.inject(DisplaysService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
