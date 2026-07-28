import {ComponentFixture, TestBed} from '@angular/core/testing';
import {provideHttpClient} from '@angular/common/http';
import {provideHttpClientTesting} from '@angular/common/http/testing';
import {provideRouter} from '@angular/router';
import {MatButtonModule} from '@angular/material/button';
import {MatIconModule} from '@angular/material/icon';
import {MatSidenavModule} from '@angular/material/sidenav';
import {MatSlideToggleModule} from '@angular/material/slide-toggle';
import {MatToolbarModule} from '@angular/material/toolbar';
import {KtdGridModule} from '@katoid/angular-grid-layout';
import {NoopAnimationsModule} from '@angular/platform-browser/animations';

import {AppComponent} from './app.component';
import {DevicePaneComponent} from './device-pane/device-pane.component';
import {ViewPaneComponent} from './view-pane/view-pane.component';
import {SafeDeviceUrlPipe} from './safe-device-url.pipe';
import {BUILD_VERSION} from '../environments/version';

describe('AppComponent', () => {
  let fixture: ComponentFixture<AppComponent>;
  let app: AppComponent;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [
        AppComponent,
        DevicePaneComponent,
        ViewPaneComponent,
        SafeDeviceUrlPipe,
      ],
      imports: [
        MatButtonModule,
        MatIconModule,
        MatSidenavModule,
        MatSlideToggleModule,
        MatToolbarModule,
        KtdGridModule,
        NoopAnimationsModule,
      ],
      providers: [
        provideHttpClient(),
        provideHttpClientTesting(),
        provideRouter([]),
      ],
    }).compileComponents();

    fixture = TestBed.createComponent(AppComponent);
    fixture.detectChanges();
    app = fixture.componentInstance;
  });

  it('should create the app', () => {
    expect(app).toBeTruthy();
  });

  it('should set version to BUILD_VERSION', () => {
    expect(app.version).toEqual(BUILD_VERSION);
  });

  it('should render title', () => {
    const compiled = fixture.nativeElement as HTMLElement;
    expect(
      compiled.querySelector('.cloud-android-title')?.textContent,
    ).toContain('Cloud Android');
  });
});
