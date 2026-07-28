import {ComponentFixture, TestBed} from '@angular/core/testing';
import {provideHttpClient} from '@angular/common/http';
import {provideHttpClientTesting} from '@angular/common/http/testing';
import {provideRouter} from '@angular/router';
import {MatButtonModule} from '@angular/material/button';
import {MatIconModule} from '@angular/material/icon';
import {MatSlideToggleModule} from '@angular/material/slide-toggle';
import {NoopAnimationsModule} from '@angular/platform-browser/animations';

import {DevicePaneComponent} from './device-pane.component';

describe('DevicePaneComponent', () => {
  let component: DevicePaneComponent;
  let fixture: ComponentFixture<DevicePaneComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [DevicePaneComponent],
      imports: [
        MatButtonModule,
        MatIconModule,
        MatSlideToggleModule,
        NoopAnimationsModule,
      ],
      providers: [
        provideHttpClient(),
        provideHttpClientTesting(),
        provideRouter([]),
      ],
    }).compileComponents();

    fixture = TestBed.createComponent(DevicePaneComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
