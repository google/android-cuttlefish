import {Injectable, SecurityContext} from '@angular/core';
import {map, ReplaySubject, Subject} from 'rxjs';
import {HttpClient} from '@angular/common/http';
import {Device} from './device-interface';
import {DomSanitizer} from '@angular/platform-browser';

@Injectable({
  providedIn: 'root',
})
export class DeviceService {
  private devicesSubject: Subject<Device[]> = new ReplaySubject<Device[]>(1);
  private devicesObservable = this.devicesSubject.asObservable();

  constructor(
    private readonly httpClient: HttpClient,
    private sanitizer: DomSanitizer
  ) {}

  refresh(): void {
    this.httpClient
      .get<string[]>('./devices')
      .pipe(
        map((deviceIds: string[]) =>
          deviceIds.sort().map(this.createDevice.bind(this))
        )
      )
      .subscribe((devices: Device[]) => this.devicesSubject.next(devices));
  }

  deviceConnectURL(display: string): string {
    return this.sanitizer.sanitize(
      SecurityContext.RESOURCE_URL,
      this.sanitizer.bypassSecurityTrustResourceUrl(
        `/devices/${display}/files/client.html`
      )
    ) as string;
  }

  createDevice(deviceId: string): Device {
    return new Device(deviceId, this.deviceConnectURL(deviceId));
  }

  getDevices() {
    this.refresh();
    return this.devicesObservable;
  }
}
