import {Injectable} from '@angular/core';
import {map, mergeMap, shareReplay} from 'rxjs/operators';
import {Subject, merge} from 'rxjs';
import {HttpClient} from '@angular/common/http';
import {DomSanitizer} from '@angular/platform-browser';

@Injectable({
  providedIn: 'root',
})
export class GroupService {
  private refreshSubject = new Subject<void>();
  private groupsFromServer = this.httpClient
    .get<string[]>('./groups')
    .pipe(
      map((deviceIds: string[]) =>
        deviceIds.sort((a, b) => a.localeCompare(b, undefined, {numeric: true}))
      )
    );

  private groups = merge(
    this.groupsFromServer,
    this.refreshSubject.pipe(mergeMap(() => this.groupsFromServer))
  ).pipe(shareReplay(1));

  constructor(
    private readonly httpClient: HttpClient,
  ) {}

  refresh(): void {
    this.refreshSubject.next();
  }

  getGroups() {
    return this.groups;
  }
}
