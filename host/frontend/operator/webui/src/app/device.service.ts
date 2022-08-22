import {Injectable, SecurityContext} from '@angular/core';
import {map, ReplaySubject, Subject} from 'rxjs';
import {HttpClient} from '@angular/common/http';
import {Device} from './device-interface';
import {DomSanitizer} from '@angular/platform-browser';

@Injectable({
  providedIn: 'root',
})
export class DeviceService {

  private devicesSubject : Subject<Device[]> = new ReplaySubject<Device[]>(1);
  private devicesObservable = this.devicesSubject.asObservable();

  constructor(private readonly httpClient: HttpClient, private sanitizer:DomSanitizer) {}

  refresh(): void {
    this.httpClient.get<string[]>('./devices').pipe(map((devices) => devices.sort()))
    .pipe(map((data) => data.map((deviceId) => new Device(deviceId, this.deviceConnectURL(deviceId))))).subscribe((devices) => this.devicesSubject.next(devices));
  }

  deviceConnectURL(display: string) : string{
    return this.sanitizer.sanitize(
      SecurityContext.RESOURCE_URL,
      this.sanitizer.bypassSecurityTrustResourceUrl(
        `/devices/${display}/files/client.html`
      )
    ) as string;
  }

  getDevices() {
    this.refresh();
    return this.devicesObservable;
  }
}
