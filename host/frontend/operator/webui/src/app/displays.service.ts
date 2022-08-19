import {Injectable, SecurityContext} from '@angular/core';
import {DomSanitizer} from '@angular/platform-browser';
import {BehaviorSubject} from 'rxjs';

@Injectable({
  providedIn: 'root',
})
export class DisplaysService {
  private displays: string[] = [];
  private visibleDevicesChanged = new BehaviorSubject<string[]>([]);
  
  constructor(private sanitizer: DomSanitizer) {}

  add(display: string) {
    if (!this.displays.includes(display)) {
      this.displays.push(display);
    }
    this.visibleDevicesChanged.next(this.displays);
  }

  remove(display: string) {
    if (this.displays.includes(display)){
      this.displays = this.displays.filter(element => element !== display);
    }
    this.visibleDevicesChanged.next(this.displays);
  }

  visibleValidate(display: string) : boolean {
    return this.displays.includes(display);
  }

  getVisibleDevices() {
    return this.visibleDevicesChanged.asObservable();
  }

  deviceConnectURL(display: string): string {
    return this.sanitizer.sanitize(
      SecurityContext.RESOURCE_URL,
      this.sanitizer.bypassSecurityTrustResourceUrl(
        `/devices/${display}/files/client.html`
      )
    ) as string;
  }
}
