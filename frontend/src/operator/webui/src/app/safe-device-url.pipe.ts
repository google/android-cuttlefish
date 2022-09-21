import {Pipe, PipeTransform} from '@angular/core';
import {DomSanitizer} from '@angular/platform-browser';

@Pipe({
  name: 'safedeviceurl',
})
export class SafeDeviceUrlPipe implements PipeTransform {
  constructor(private sanitizer: DomSanitizer) {}

  transform(deviceId: string) {
    return this.sanitizer.bypassSecurityTrustResourceUrl(
      `/devices/${deviceId}/files/client.html`
    );
  }
}
