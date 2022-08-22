import {Injectable, SecurityContext} from '@angular/core';
import {DomSanitizer} from '@angular/platform-browser';
import {BehaviorSubject, map} from 'rxjs';
import {DeviceService} from './device.service';
import {Device} from './device-interface';

@Injectable({
  providedIn: 'root',
})
export class DisplaysService {
  private displays: Device[] = [];
  private visibleDevicesChanged = new BehaviorSubject<Device[]>([]);
  
  constructor(private deviceService: DeviceService, private sanitizer: DomSanitizer) {}

  refresh() {
    this.deviceService.getDevices().subscribe((devices) => {
      this.displays = this.displays.filter((display) => devices.includes(display))
      this.visibleDevicesChanged.next(this.displays);
    });
  }

  add(display: Device) {
    if (!this.displays.includes(display)) {
      this.displays.push(display);
    }
    this.visibleDevicesChanged.next(this.displays);
  }

  remove(display: Device) {
    if (this.displays.includes(display)){
      this.displays = this.displays.filter(element => element !== display);
    }
    this.visibleDevicesChanged.next(this.displays);
  }

  visibleValidate(display: Device) : boolean {
    return this.displays.includes(display);
  }

  getVisibleDevices() {
    return this.visibleDevicesChanged.asObservable();
  }

  // deviceConnectURL(display: Device) {
  //   display.url = this.sanitizer.sanitize(
  //     SecurityContext.RESOURCE_URL,
  //     this.sanitizer.bypassSecurityTrustResourceUrl(
  //       `/devices/${display}/files/client.html`
  //     )
  //   ) as string;
  // }
}
