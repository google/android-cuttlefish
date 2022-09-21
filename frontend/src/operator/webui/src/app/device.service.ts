import {Injectable} from '@angular/core';
import {map} from 'rxjs/operators';
import {Observable, ReplaySubject, Subject} from 'rxjs';
import {HttpClient} from '@angular/common/http';
import {Device} from './device-interface';
import {DeviceInfo} from './device-info-interface';
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

  createDevice(deviceId: string): Device {
    return new Device(deviceId);
  }

  getDevices() {
    this.refresh();
    return this.devicesObservable;
  }

  getDeviceInfo(deviceId: string): Observable<DeviceInfo> {
    return this.httpClient
      .get('./devices/' + deviceId)
      .pipe(map(res => res as DeviceInfo));
  }
}
