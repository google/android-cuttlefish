import {Injectable} from '@angular/core';
import {BehaviorSubject} from 'rxjs';
import {DeviceService} from './device.service';
import {Device} from './device-interface';

@Injectable({
  providedIn: 'root',
})
export class DisplaysService {
  private visibleDevices: Device[] = [];
  private visibleDevicesChanged = new BehaviorSubject<Device[]>([]);

  constructor(private deviceService: DeviceService) {
    this.deviceService.getDevices().subscribe((allDevices: Device[]) => {
      this.visibleDevices = this.visibleDevices.filter(visibleDevice =>
        allDevices.some(aDevice => aDevice.deviceId === visibleDevice.deviceId)
      );
      this.visibleDevicesChanged.next(this.visibleDevices);
    });
  }

  add(addedDevice: Device) {
    if (
      !this.visibleDevices.some(
        visibleDevice => visibleDevice.deviceId === addedDevice.deviceId
      )
    ) {
      this.visibleDevices.push(addedDevice);
    }
    this.visibleDevicesChanged.next(this.visibleDevices);
  }

  remove(removedDevice: Device) {
    this.visibleDevices = this.visibleDevices.filter(
      visibleDevice => visibleDevice.deviceId !== removedDevice.deviceId
    );
    this.visibleDevicesChanged.next(this.visibleDevices);
  }

  isVisibleDevice(display: Device): boolean {
    return this.visibleDevices.some(
      device => display.deviceId === device.deviceId
    );
  }

  getVisibleDevices() {
    return this.visibleDevicesChanged.asObservable();
  }
}
