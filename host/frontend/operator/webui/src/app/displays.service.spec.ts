import { TestBed } from '@angular/core/testing';

import { DisplaysService } from './displays.service';

describe('DisplaysService', () => {
  let service: DisplaysService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(DisplaysService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
