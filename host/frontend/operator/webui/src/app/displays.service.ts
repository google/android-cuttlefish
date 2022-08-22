import {Injectable} from '@angular/core';
import {BehaviorSubject} from 'rxjs';
import {DeviceService} from './device.service';
import {Device} from './device-interface';

@Injectable({
  providedIn: 'root',
})
export class DisplaysService {
  private displays: Device[] = [];
  private visibleDevicesChanged = new BehaviorSubject<Device[]>([]);
  
  constructor(private deviceService: DeviceService) {
    this.deviceService.getDevices().subscribe((devices) => {
      this.displays = this.displays.filter((display) => devices.some((device) => device.deviceId == display.deviceId))
      this.visibleDevicesChanged.next(this.displays);
    });
  }

  add(display: Device) {
    if (!this.displays.find((device) => device.deviceId == display.deviceId)) {
      this.displays.push(display);
    }
    this.visibleDevicesChanged.next(this.displays);
  }

  remove(display: Device) {
    this.displays = this.displays.filter((device) => device.deviceId != display.deviceId);
    this.visibleDevicesChanged.next(this.displays);
  }

  isVisibleDevice(display: Device) : boolean {
    return this.displays.some((device) => display.deviceId == device.deviceId)
  }

  getVisibleDevices() {
    return this.visibleDevicesChanged.asObservable();
  }
}
