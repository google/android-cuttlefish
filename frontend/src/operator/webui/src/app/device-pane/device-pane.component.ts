import {Component} from '@angular/core';
import {DeviceService} from '../device.service';
import {DisplaysService} from '../displays.service';
import {first} from 'rxjs/operators';

@Component({
  selector: 'app-device-pane',
  templateUrl: './device-pane.component.html',
  styleUrls: ['./device-pane.component.scss'],
})
export class DevicePaneComponent {
  devices = this.deviceService.getDevices();

  constructor(
    private deviceService: DeviceService,
    public displaysService: DisplaysService
  ) {}

  ngOnInit(): void {
    this.deviceService.refresh();
  }

  onRefresh(): void {
    this.deviceService.refresh();
  }

  showAll(): void {
    this.devices.pipe(first()).subscribe((devices: string[]) => {
      devices.forEach(device => {
        if (!this.displaysService.isVisibleDevice(device)) {
          this.displaysService.toggleVisibility(device);
        }
      });
    });
  }
}
