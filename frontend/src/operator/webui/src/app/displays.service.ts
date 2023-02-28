import {Injectable} from '@angular/core';
import {Subject, merge} from 'rxjs';
import {map, mergeMap} from 'rxjs/operators';
import {DeviceService} from './device.service';

interface DisplayInfo {
  display_id: string;
  width: number;
  height: number;
}

interface DeviceDisplays {
  device_id: string;
  rotation: number;
  displays: DisplayInfo[];
}

@Injectable({
  providedIn: 'root',
})
export class DisplaysService {
  private devices = this.deviceService.getDevices();

  private visibleDeviceIds: string[] = [];
  private visibleDevicesChanged = new Subject<void>();

  private deviceVisibilityInfos = merge(
    this.devices,
    this.visibleDevicesChanged.pipe(mergeMap(() => this.devices))
  ).pipe(
    map(deviceIds =>
      deviceIds.map(deviceId => {
        return {id: deviceId, visible: this.isVisibleDevice(deviceId)};
      })
    )
  );

  private displayInfoChanged = new Subject<DeviceDisplays>();

  constructor(private deviceService: DeviceService) {}

  toggleVisibility(deviceId: string): void {
    if (this.isVisibleDevice(deviceId)) {
      this.removeFromVisibleDevices(deviceId);
    } else {
      this.addToVisibleDevices(deviceId);
    }

    this.visibleDevicesChanged.next();
  }

  isVisibleDevice(display: string): boolean {
    return this.visibleDeviceIds.some(device => display === device);
  }

  getDeviceVisibilities() {
    return this.deviceVisibilityInfos;
  }

  getDisplayInfoChanged() {
    return this.displayInfoChanged.asObservable();
  }

  onDeviceDisplayInfo(deviceDisplays: DeviceDisplays) {
    if (deviceDisplays.displays.length === 0) return;

    this.displayInfoChanged.next(deviceDisplays);
  }

  private addToVisibleDevices(addedDevice: string) {
    if (!this.isVisibleDevice(addedDevice)) {
      this.visibleDeviceIds.push(addedDevice);
    }
  }

  private removeFromVisibleDevices(removedDevice: string) {
    this.visibleDeviceIds = this.visibleDeviceIds.filter(
      visibleDevice => visibleDevice !== removedDevice
    );
  }
}
