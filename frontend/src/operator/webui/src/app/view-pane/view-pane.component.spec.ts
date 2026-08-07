import {ComponentFixture, TestBed} from '@angular/core/testing';
import {provideHttpClient} from '@angular/common/http';
import {provideHttpClientTesting} from '@angular/common/http/testing';
import {KtdGridModule} from '@katoid/angular-grid-layout';

import {ViewPaneComponent} from './view-pane.component';
import {SafeDeviceUrlPipe} from '../safe-device-url.pipe';

describe('ViewPaneComponent', () => {
  let component: ViewPaneComponent;
  let fixture: ComponentFixture<ViewPaneComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ViewPaneComponent, SafeDeviceUrlPipe],
      imports: [KtdGridModule],
      providers: [provideHttpClient(), provideHttpClientTesting()],
    }).compileComponents();

    fixture = TestBed.createComponent(ViewPaneComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
