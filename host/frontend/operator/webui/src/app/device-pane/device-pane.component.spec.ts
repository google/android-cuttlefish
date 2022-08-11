import {ComponentFixture, TestBed} from '@angular/core/testing';

import {DevicePaneComponent} from './device-pane.component';

describe('DevicePaneComponent', () => {
  let component: DevicePaneComponent;
  let fixture: ComponentFixture<DevicePaneComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [DevicePaneComponent],
    }).compileComponents();

    fixture = TestBed.createComponent(DevicePaneComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
