import {Injectable, OnDestroy, NgZone} from '@angular/core';

function getSavedTheme(): string | null {
  try {
    return window.localStorage.getItem('theme');
  } catch {
    return null;
  }
}

function setSavedTheme(theme: string): void {
  try {
    window.localStorage.setItem('theme', theme);
  } catch {
    // Ignore.
  }
}

/**
 * Manages light and dark theme state, system preference detection
 * (prefers-color-scheme), and persisting user theme preference in localStorage.
 */
@Injectable({
  providedIn: 'root',
})
export class ThemeService implements OnDestroy {
  isDarkMode = false;
  private query?: MediaQueryList;
  private queryListener?: (e: MediaQueryListEvent) => void;

  constructor(private ngZone: NgZone) {
    const savedTheme = getSavedTheme();
    if (savedTheme !== null) {
      this.isDarkMode = savedTheme === 'dark';
    } else {
      this.listenToPrefersColorSchemeChanges();
    }
  }

  toggleDarkMode(): void {
    this.isDarkMode = !this.isDarkMode;
    setSavedTheme(this.isDarkMode ? 'dark' : 'light');
    this.stopListeningToPrefersColorSchemeChanges();
  }

  private listenToPrefersColorSchemeChanges(): void {
    if (window.matchMedia) {
      this.query = window.matchMedia('(prefers-color-scheme: dark)');
      this.queryListener = (e: MediaQueryListEvent) => {
        if (getSavedTheme() === null) {
          this.ngZone.run(() => (this.isDarkMode = e.matches));
        }
      };
      this.query.addEventListener('change', this.queryListener);
      this.isDarkMode = this.query.matches;
    }
  }

  private stopListeningToPrefersColorSchemeChanges(): void {
    if (this.queryListener) {
      this.query?.removeEventListener('change', this.queryListener);
      this.query = undefined;
      this.queryListener = undefined;
    }
  }

  ngOnDestroy(): void {
    this.stopListeningToPrefersColorSchemeChanges();
  }
}
