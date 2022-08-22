import {Component} from '@angular/core';
import {Device} from '../device-interface';
import {DeviceService} from '../device.service';
import {DisplaysService} from '../displays.service';
import {first} from 'rxjs';

@Component({
  selector: 'app-device-pane',
  templateUrl: './device-pane.component.html',
  styleUrls: ['./device-pane.component.sass'],
})
export class DevicePaneComponent {
  devices = this.deviceService.getDevices();

  constructor(
    private deviceService: DeviceService,
    public displaysService: DisplaysService
  ) {}

  onSelect(device: Device): void {
    if (this.displaysService.isVisibleDevice(device)) {
      this.displaysService.remove(device);
    } else {
      this.displaysService.add(device);
    }
  }

  onRefresh(): void {
    this.deviceService.refresh();
  }

  showAll(): void {
    this.devices.pipe(first()).subscribe((devices: Device[]) => {
      devices.forEach(device => {
        if (!this.displaysService.isVisibleDevice(device)) {
          this.onSelect(device);
        }
      });
    });
  }
}
