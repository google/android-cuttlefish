import {TestBed} from '@angular/core/testing';

import {ThemeService} from './theme.service';

describe('ThemeService', () => {
  beforeEach(() => {
    window.localStorage.removeItem('theme');
    TestBed.configureTestingModule({
      providers: [ThemeService],
    });
  });

  afterEach(() => {
    window.localStorage.removeItem('theme');
  });

  it('should be created', () => {
    const service = TestBed.inject(ThemeService);
    expect(service).toBeTruthy();
  });

  it('should toggle dark mode state and save to localStorage', () => {
    const service = TestBed.inject(ThemeService);
    const initialState = service.isDarkMode;
    service.toggleDarkMode();
    expect(service.isDarkMode).toBe(!initialState);
    expect(window.localStorage.getItem('theme')).toBe(
      service.isDarkMode ? 'dark' : 'light',
    );
  });

  it('should respect saved theme from localStorage on initialization', () => {
    window.localStorage.setItem('theme', 'dark');
    const service = TestBed.inject(ThemeService);
    expect(service.isDarkMode).toBe(true);
  });

  describe('system theme preference', () => {
    let queryListener: ((e: MediaQueryListEvent) => void) | undefined;
    let removeEventListenerSpy: jasmine.Spy;

    beforeEach(() => {
      queryListener = undefined;
      removeEventListenerSpy = jasmine.createSpy('removeEventListener');

      spyOn(window, 'matchMedia').and.callFake((query: string) => {
        return {
          matches: false,
          addEventListener: (
            event: string,
            listener: (e: MediaQueryListEvent) => void,
          ) => (queryListener = listener),
          removeEventListener: removeEventListenerSpy,
        } as unknown as MediaQueryList;
      });
    });

    it('should use system theme when no theme is saved', () => {
      const service = TestBed.inject(ThemeService);
      expect(service.isDarkMode).toBe(false);
      expect(queryListener).toBeDefined();

      queryListener!({matches: true} as MediaQueryListEvent);
      expect(service.isDarkMode).toBe(true);

      queryListener!({matches: false} as MediaQueryListEvent);
      expect(service.isDarkMode).toBe(false);
    });

    it('should ignore system theme when a saved theme is present', () => {
      window.localStorage.setItem('theme', 'dark');
      const service = TestBed.inject(ThemeService);
      expect(service.isDarkMode).toBe(true);
      expect(queryListener).toBeUndefined();
    });

    it('should remove listener on ngOnDestroy', () => {
      const service = TestBed.inject(ThemeService);
      service.ngOnDestroy();

      expect(removeEventListenerSpy).toHaveBeenCalledWith(
        'change',
        queryListener,
      );
    });
  });
});
